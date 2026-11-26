#include "SaveTask.h"

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace SQM
{
    namespace SaveTask
    {
        namespace
        {
            std::atomic<uint32_t> stackLeft{0};

            struct Job
            {
                const std::function<void()> *work;
                SemaphoreHandle_t done;
            };

            void entry(void *arg)
            {
                Job *job = static_cast<Job *>(arg);
                (*job->work)();
                stackLeft.store(static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr))); // bytes on ESP-IDF
                xSemaphoreGive(job->done); // `job` lives on the caller's stack: not touched after this
                vTaskDelete(nullptr);
            }
        } // namespace

        bool run(const std::function<void()> &work)
        {
            StaticSemaphore_t storage;
            SemaphoreHandle_t done = xSemaphoreCreateBinaryStatic(&storage);
            Job job{&work, done};
            // Beside the caller, at its priority: it waits meanwhile, as it did for the save.
            const BaseType_t created = xTaskCreatePinnedToCore(
                entry, "config_save", STACK_BYTES, &job, uxTaskPriorityGet(nullptr), nullptr, static_cast<BaseType_t>(xPortGetCoreID()));
            if (created == pdPASS)
                xSemaphoreTake(done, portMAX_DELAY);
            vSemaphoreDelete(done);
            return created == pdPASS;
        }

        uint32_t lastStackLeft()
        {
            return stackLeft.load();
        }
    } // namespace SaveTask
} // namespace SQM
