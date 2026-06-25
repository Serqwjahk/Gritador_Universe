#pragma once
#include <thread>
#include <vector>
#include <atomic>
#include <functional>
#include <condition_variable>
#include <mutex>
#include <algorithm>

// ============================================================
//  Pool de hilos persistente para paralelizar bucles "for"
// ============================================================
//
// StepPhysics llama a ComputeAccelerations y UpdateDustGravity una vez por
// cada SUB-PASO de integracion, y ese numero de sub-pasos es ADAPTATIVO
// (8 veces por frame a velocidad 1x, hasta PHYS_MAX_SUBSTEPS_PER_FRAME=64
// a velocidades altas -- ver constants.h/StepPhysics). Crear hilos nuevos
// en cada una de esas llamadas (std::thread/std::async) tendria un costo
// de creacion/destruccion que, multiplicado por hasta ~192 veces por frame
// a 60fps, se comeria la mayor parte de la ganancia.
//
// Este pool crea los hilos trabajadores UNA SOLA VEZ al arrancar y los
// deja dormidos en una condition_variable. ParallelFor() reparte un rango
// [0,count) entre los workers + el hilo que llama (round-robin via un
// indice atomico, "work stealing" trivial) y bloquea hasta que TODOS los
// hilos trabajadores hayan retornado de su ronda de trabajo para esta
// generacion (barrera por contador 'activeWorkers', no por suma de
// trabajo completado). Esto evita que un worker quede "en vuelo" leyendo
// 'job'/'taskTotal' (o memoria capturada por la lambda, p.ej. el array
// 'sources' de UpdateDustGravity) justo cuando ParallelFor ya retorno y
// la siguiente llamada sobreescribe ese estado global.
class ThreadPool {
public:
    explicit ThreadPool(unsigned numWorkers) {
        for (unsigned i = 0; i < numWorkers; ++i)
            workers.emplace_back([this] { WorkerLoop(); });
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lk(mtx);
            stop = true;
            ++generation;
        }
        cv.notify_all();
        for (auto& t : workers) t.join();
    }

    // Numero total de hilos disponibles para trabajo (workers + el que llama).
    unsigned ConcurrencyHint() const { return (unsigned)workers.size() + 1; }

    // Ejecuta fn(i) para cada i en [0, count). Bloquea hasta terminar.
    void ParallelFor(int count, const std::function<void(int)>& fn) {
        if (count <= 0) return;
        if (workers.empty() || count == 1) {
            for (int i = 0; i < count; ++i) fn(i);
            return;
        }

        job       = &fn;
        taskTotal = count;
        nextIndex.store(0, std::memory_order_relaxed);
        activeWorkers.store((int)workers.size(), std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lk(mtx);
            ++generation;
        }
        cv.notify_all();

        RunTasks();

        // Espera a que TODOS los workers hayan retornado de su RunTasks()
        // para esta generacion (no solo a que la suma de indices procesados
        // cuadre). Spin corto primero (el reparto deberia terminar casi a la
        // vez que el trabajo del propio hilo principal); si tarda mas,
        // bloquea con una condition_variable en vez de quemar un core.
        int spins = 0;
        while (activeWorkers.load(std::memory_order_acquire) != 0) {
            if (++spins < 1000) { std::this_thread::yield(); continue; }
            std::unique_lock<std::mutex> lk(mtx);
            doneCv.wait(lk, [this] { return activeWorkers.load(std::memory_order_acquire) == 0; });
            break;
        }
    }

private:
    void RunTasks() {
        int i;
        while ((i = nextIndex.fetch_add(1, std::memory_order_relaxed)) < taskTotal)
            (*job)(i);
    }

    void WorkerLoop() {
        unsigned long lastGen = 0;
        while (true) {
            std::unique_lock<std::mutex> lk(mtx);
            cv.wait(lk, [this, lastGen] { return stop || generation != lastGen; });
            if (stop) return;
            lastGen = generation;
            lk.unlock();

            RunTasks();

            if (activeWorkers.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::lock_guard<std::mutex> lk2(mtx);
                doneCv.notify_one();
            }
        }
    }

    std::vector<std::thread> workers;
    std::mutex              mtx;
    std::condition_variable cv;
    std::condition_variable doneCv;
    bool stop = false;
    unsigned long generation = 0;

    std::atomic<int> nextIndex{0};
    std::atomic<int> activeWorkers{0};
    int taskTotal = 0;
    const std::function<void(int)>* job = nullptr;
};

// Instancia global unica: 'hardware_concurrency() - 1' hilos trabajadores
// adicionales (el hilo principal tambien procesa tareas), acotado a un
// rango razonable para no desperdiciar tiempo repartiendo trabajo cuando
// hay pocos cuerpos/particulas.
inline ThreadPool& GetThreadPool() {
    unsigned hc = std::thread::hardware_concurrency();
    unsigned workers = (hc > 1) ? std::clamp(hc - 1, 1u, 8u) : 1u;
    static ThreadPool pool(workers);
    return pool;
}
