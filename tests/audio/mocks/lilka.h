#pragma once
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
using TickType_t = unsigned;
struct Semaphore {
    std::mutex mutex;
    std::condition_variable cv;
    bool available;
    explicit Semaphore(bool value): available(value) {}
};
using SemaphoreHandle_t = Semaphore*;
using TaskHandle_t = void*;
constexpr unsigned portMAX_DELAY = ~0u;
constexpr int pdPASS = 1;
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_INTR_FLAG_LEVEL1 1
inline std::thread audioThread;
inline int failSemaphore = 0;
inline bool failTask = false;
inline int semaphoreCount = 0;
inline thread_local int locksHeld = 0;
inline SemaphoreHandle_t createSemaphore(bool available) {
    if (failSemaphore && --failSemaphore == 0) return nullptr;
    ++semaphoreCount;
    return new Semaphore(available);
}
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return createSemaphore(true); }
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return createSemaphore(false); }
inline int xSemaphoreTake(SemaphoreHandle_t sem, unsigned) {
    std::unique_lock<std::mutex> lock(sem->mutex);
    sem->cv.wait(lock, [sem] { return sem->available; });
    sem->available = false;
    ++locksHeld;
    return 1;
}
inline int xSemaphoreGive(SemaphoreHandle_t sem) {
    std::lock_guard<std::mutex> lock(sem->mutex);
    sem->available = true;
    if (locksHeld) --locksHeld;
    sem->cv.notify_one();
    return 1;
}
inline void vSemaphoreDelete(SemaphoreHandle_t sem) { --semaphoreCount; delete sem; }
inline int xTaskCreatePinnedToCore(void (*task)(void*), const char*, unsigned, void* arg, int, TaskHandle_t*, int) {
    if (failTask) return 0;
    audioThread = std::thread(task, arg);
    return pdPASS;
}
inline void vTaskDelay(unsigned) { std::this_thread::yield(); }
inline void vTaskDelete(void*) {}
namespace lilka {
struct Audio {
    static inline std::atomic<int> volume{100};
    static int getVolume() { return volume.load(); }
    static void initPins() {}
};
inline Audio audio;
inline void serial_log(const char*) {}
}
