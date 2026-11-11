#include "FirmwareImage.h"
#include "FirmwareMarker.h"
#include "Logger.h"
#include "WebServer.h"
#include <LittleFS.h>
#include <Update.h>

#include "FlashGuard.h"
#include "PlatformGuard.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>

// Firmware and web UI updates by upload (/api/update, /api/update/fs). There is
// no release check: by design (spec 001).

namespace SQM
{
    namespace
    {
        // POST /api/update/fs: the LittleFS image, written straight to the
        // partition (no magic byte to check).
        struct FsUpload
        {
            const esp_partition_t *partition = nullptr;
            size_t bytesWritten = 0;
            bool error = false;
            bool touched = false; // LittleFS unmounted and the partition erased
            String errorMessage = "";

            void fail(const char *message)
            {
                error = true;
                errorMessage = message;
            }
        };
        FsUpload fsUpload;

        // POST /api/update: set when the updater's own activation failed but
        // a second esp_ota_set_boot_partition() (which re-verifies the
        // image) succeeded.
        bool activatedOnRetry = false;

        // POST /api/update: the image's marker (SQMeter spec 027 FR-020), and why it
        // was refused (empty when it wasn't).
        FirmwareImage::Scanner uploadScanner;
        std::string uploadRefusal;

        // POST /api/update: set once the last chunk has arrived. A connection
        // that drops before then (a stalled WiFi transfer, a closed browser
        // tab) leaves a half-written update; it is abandoned at once so the
        // next upload starts clean instead of hanging behind it.
        bool firmwareUploadComplete = false;
        // The request the running update belongs to: a dropped connection
        // is often noticed only after the next upload has started, and must
        // not abandon that one.
        const AsyncWebServerRequest *firmwareUploadRequest = nullptr;

        void abandonUnfinishedUpload(const AsyncWebServerRequest *request)
        {
            if (request != firmwareUploadRequest)
                return;
            firmwareUploadRequest = nullptr;
            if (firmwareUploadComplete || !Update.isRunning())
                return;
            Logger::warn("OTA", "Upload connection dropped before the end; update abandoned");
            Update.abort();
        }

        // Multipart framing around the image in an upload's body.
        constexpr size_t FORM_OVERHEAD = 4096;

        void fsUploadBegin(const String &filename, size_t requestBytes)
        {
            Logger::info("OTA", "Filesystem update started: %s", filename.c_str());
            fsUpload = FsUpload{};

            // Find the LittleFS partition (labeled as "spiffs" in partition table)
            fsUpload.partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
            if (!fsUpload.partition)
            {
                Logger::error("OTA", "Filesystem partition not found!");
                fsUpload.fail("Filesystem partition not found");
                return;
            }
            // An image for another layout would be cut off or leave a
            // filesystem that doesn't mount: refuse it before anything is erased.
            if (!FirmwareImage::fsImageFits(requestBytes, fsUpload.partition->size, FORM_OVERHEAD))
            {
                Logger::error(
                    "OTA", "Filesystem image is the wrong size for this device (%u bytes sent)", static_cast<unsigned>(requestBytes));
                fsUpload.fail("Web UI file is for a different device layout. Use this release's littlefs file.");
                return;
            }
            Logger::info(
                "OTA",
                "Found filesystem partition at 0x%x, size %u bytes",
                fsUpload.partition->address,
                static_cast<unsigned>(fsUpload.partition->size));

            // Unmount LittleFS before writing
            fsUpload.touched = true;
            LittleFS.end();

            Logger::info("OTA", "Erasing filesystem partition...");
            esp_err_t err = esp_partition_erase_range(fsUpload.partition, 0, fsUpload.partition->size);
            if (err != ESP_OK)
            {
                Logger::error("OTA", "Partition erase failed: %d", err);
                fsUpload.fail("Failed to erase partition");
                return;
            }
            Logger::info("OTA", "Partition erased successfully");
        }

        // False when this write failed (the upload is then abandoned).
        bool fsUploadWrite(size_t index, const uint8_t *data, size_t len)
        {
            if (fsUpload.error || !fsUpload.partition)
                return true;
            esp_err_t err = esp_partition_write(fsUpload.partition, fsUpload.bytesWritten, data, len);
            if (err != ESP_OK)
            {
                Logger::error("OTA", "Partition write failed at offset %u: %d", static_cast<unsigned>(fsUpload.bytesWritten), err);
                fsUpload.fail("Failed to write to partition");
                return false;
            }
            fsUpload.bytesWritten += len;
            if (index % 10240 == 0) // Log every ~10KB
                Logger::info("OTA", "Written %u bytes", static_cast<unsigned>(fsUpload.bytesWritten));
            return true;
        }

        struct UploadChunk
        {
            size_t index;
            uint8_t *data;
            size_t len;
            bool final;
        };

        void fsUploadChunk(const String &filename, size_t requestBytes, const UploadChunk &chunk)
        {
            if (!chunk.index)
            {
                fsUploadBegin(filename, requestBytes);
                if (fsUpload.error)
                    return;
            }
            if (!fsUploadWrite(chunk.index, chunk.data, chunk.len) || !chunk.final)
                return;
            if (!fsUpload.error)
                Logger::info("OTA", "Filesystem update success: %u bytes written", static_cast<unsigned>(fsUpload.bytesWritten));
            else
                Logger::error("OTA", "Filesystem update failed: %s", fsUpload.errorMessage.c_str());
        }

        const char *updateErrorText(uint8_t error)
        {
            switch (error)
            {
            case UPDATE_ERROR_OK:
                return "No error";
            case UPDATE_ERROR_WRITE:
                return "Flash write failed";
            case UPDATE_ERROR_ERASE:
                return "Flash erase failed";
            case UPDATE_ERROR_READ:
                return "Flash read failed";
            case UPDATE_ERROR_SPACE:
                return "Not enough space";
            case UPDATE_ERROR_SIZE:
                return "Bad size given";
            case UPDATE_ERROR_STREAM:
                return "Stream read timeout";
            case UPDATE_ERROR_MD5:
                return "MD5 check failed";
            case UPDATE_ERROR_MAGIC_BYTE:
                return "Wrong magic byte";
            case UPDATE_ERROR_ACTIVATE:
                return "Could not activate partition";
            case UPDATE_ERROR_NO_PARTITION:
                return "Partition not found";
            case UPDATE_ERROR_BAD_ARGUMENT:
                return "Bad argument";
            case UPDATE_ERROR_ABORT:
                return "Update aborted";
            default:
                return nullptr;
            }
        }

        // The image is fully written and its first block restored; only
        // switching the boot partition failed. Uploads used to fail like this
        // on the first attempt and succeed on a retry, so retry the switch
        // here. It verifies the image again, so a bad image still can't be
        // booted.
        void retryActivation()
        {
            const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
            const esp_err_t first = esp_ota_set_boot_partition(target);
            Logger::warn("OTA", "Activating %s failed; retry: %s", target ? target->label : "?", esp_err_to_name(first));
            activatedOnRetry = first == ESP_OK;
            if (activatedOnRetry)
                Logger::info("OTA", "Firmware update success on retry, rebooting...");
        }

        void firmwareUploadEnd()
        {
            const FirmwareImage::Verdict verdict = FirmwareImage::check(uploadScanner, FirmwareMarker::layout());
            if (verdict != FirmwareImage::Verdict::Ok)
            {
                // Before Update.end(): the boot partition is never switched.
                uploadRefusal = FirmwareImage::verdictMessage(verdict, uploadScanner.product());
                Logger::error("OTA", "Firmware refused: %s", uploadRefusal.c_str());
                Update.abort();
                return;
            }
            if (Update.end(true))
            {
                Logger::info("OTA", "Firmware update success, rebooting...");
            }
            else if (Update.getError() == UPDATE_ERROR_ACTIVATE)
            {
                retryActivation();
            }
            else
            {
                Logger::error("OTA", "Update.end failed: %d", Update.getError());
                Update.printError(Serial);
            }
        }

        void firmwareUploadChunk(const String &filename, size_t index, uint8_t *data, size_t len, bool final)
        {
            if (!index)
            {
                Logger::info("OTA", "Firmware update started: %s", filename.c_str());
                activatedOnRetry = false;
                firmwareUploadComplete = false;
                uploadScanner = FirmwareImage::Scanner();
                uploadRefusal.clear();
                if (Update.isRunning())
                {
                    // An earlier upload was cut off; start clean.
                    Logger::warn("OTA", "Aborting an unfinished update");
                    Update.abort();
                }
                if (!Update.begin(UPDATE_SIZE_UNKNOWN))
                {
                    Logger::error("OTA", "Update.begin failed: %d", Update.getError());
                    Update.printError(Serial);
                }
            }
            uploadScanner.feed(data, len);
            if (Update.write(data, len) != len)
            {
                Logger::error("OTA", "Update.write failed: %d", Update.getError());
                Update.printError(Serial);
            }
            if (final)
            {
                firmwareUploadComplete = true;
                firmwareUploadEnd();
            }
        }

        void sendClosing(AsyncWebServerRequest *request, int status, const String &json)
        {
            AsyncWebServerResponse *response = request->beginResponse(status, "application/json", json);
            response->addHeader("Connection", "close");
            request->send(response);
        }
    } // namespace

    namespace
    {
        // Each upload request's own state (in the request's _tempObject, freed
        // with it): its flash-activity token and why it stopped writing.
        struct UploadState
        {
            uint32_t token;
            uint8_t refusal; // UploadRefusal
            uint8_t held;    // RoofIo::HoldReason, for UploadHeld
        };

        enum UploadRefusal : uint8_t
        {
            UploadGoing = 0,
            UploadRoofMoving = 1,
            UploadRoofClosing = 2,
            UploadHeld = 3, // refused at the start (spec 010): the roof, a waiting close or the close sequence
        };

        UploadState *uploadStateOf(AsyncWebServerRequest *request)
        {
            if (request->_tempObject == nullptr)
                request->_tempObject = calloc(1, sizeof(UploadState));
            return static_cast<UploadState *>(request->_tempObject);
        }

        void stopFirmwareWrite()
        {
            if (Update.isRunning())
                Update.abort();
        }

        void stopFsWrite()
        {
            fsUpload.fail("Upload stopped: the roof is closing");
        }
    } // namespace

    // Whether this chunk may be written (spec 003 FR-024, FR-029). Nothing is
    // written while the roof moves (flash writes stall its control task and
    // watchdog), and a close always wins: an upload in progress stops at its
    // next chunk. A dropped connection ends the flash activity at once; one
    // that is never reported stops counting after RoofIo::FLASH_STALE_MS.
    bool WebServer::uploadMayWrite(AsyncWebServerRequest *request, size_t index, bool firmware)
    {
        UploadState *state = uploadStateOf(request);
        if (state == nullptr)
            return false;
        if (index == 0)
        {
            // Checked and begun under the roof's lock (spec 010). Both kinds
            // also wait for the close sequence: a new firmware may not read its
            // RTC record, and a web UI upload ends in a restart nothing can cancel.
            RoofIo::HoldReason why = RoofIo::HoldReason::None;
            const RoofIo::PlatformAction action =
                firmware ? RoofIo::PlatformAction::FirmwareUpload : RoofIo::PlatformAction::FilesystemUpload;
            state->token = PlatformGuard::beginUpload(action, why);
            if (state->token == 0)
            {
                state->refusal = UploadHeld;
                state->held = static_cast<uint8_t>(why);
                return false;
            }
            uploadActive = true;
            const uint32_t token = state->token;
            request->onDisconnect(
                [request, token, firmware]()
                {
                    FlashGuard::end(token);
                    uploadActive = false;
                    if (firmware)
                        abandonUnfinishedUpload(request);
                });
        }
        if (state->refusal != UploadGoing)
            return false;
        // The chunk's progress is stamped under the roof's lock while the roof
        // is still (spec 010): an upload that stalled past FLASH_STALE_MS let a
        // close start, and this chunk must then not be written.
        const RoofIo::HoldReason roofNow = PlatformGuard::keepUpload(state->token);
        if (roofNow != RoofIo::HoldReason::None || FlashGuard::shouldStop(state->token))
        {
            const bool moving = roofNow == RoofIo::HoldReason::Moving;
            state->refusal = moving ? UploadRoofMoving : UploadRoofClosing;
            Logger::warn("OTA", "Upload stopped part-way: the roof is %s", moving ? "moving" : "closing");
            if (firmware)
                stopFirmwareWrite();
            else
                stopFsWrite();
            FlashGuard::end(state->token);
            uploadActive = false;
            return false;
        }
        return true;
    }

    // The upload's answer when it was stopped for the roof; false when it wasn't.
    bool WebServer::answerStoppedUpload(AsyncWebServerRequest *request)
    {
        auto *state = static_cast<UploadState *>(request->_tempObject);
        if (state == nullptr)
            return false;
        FlashGuard::end(state->token);
        if (state->refusal == UploadGoing)
            return false;
        if (!requireAuth(request))
            return true;
        const char *text = state->refusal == UploadHeld         ? RoofIo::holdText(static_cast<RoofIo::HoldReason>(state->held))
                           : state->refusal == UploadRoofMoving ? "The roof is moving: stop it first, then try again"
                                                                : "Upload stopped: the roof is closing; upload again once it has closed";
        sendClosing(request, 409, createErrorJson(text).c_str());
        return true;
    }

    void WebServer::setupOTA()
    {
        // The password is checked on the first chunk, before anything is
        // erased or written: checked only when the upload ends, an
        // unauthenticated image was already installed (it took effect at the
        // next restart). The done handler refuses such an upload with 401.

        // Filesystem OTA update (LittleFS partition).
        server.on(
            "/api/update/fs",
            HTTP_POST,
            [this](AsyncWebServerRequest *request)
            {
                uploadActive = false;
                if (answerStoppedUpload(request))
                {
                    // Stopped part-way after the filesystem was erased: it must
                    // restart. Held while the roof moves or a close waits, never cancelled.
                    if (fsUpload.touched)
                        WebServer::scheduleMandatoryRestart(1000);
                    fsUpload = FsUpload{};
                    return;
                }
                handleFsUploadDone(request);
            },
            [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
            {
                // Unauthorised uploads never count as a flash write (they can't hold up an open).
                if (!requestAuthorised(request))
                    return;
                if (!uploadMayWrite(request, index, false))
                    return;
                fsUploadChunk(filename, request->contentLength(), UploadChunk{index, data, len, final});
            });

        // Firmware OTA update (app partition). Registered after /api/update/fs:
        // this server also matches "/api/update" as a prefix of
        // "/api/update/fs", so registered first it took filesystem uploads too.
        server.on(
            "/api/update",
            HTTP_POST,
            [this](AsyncWebServerRequest *request)
            {
                uploadActive = false;
                if (answerStoppedUpload(request))
                    return;
                handleFirmwareUploadDone(request);
            },
            [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
            {
                if (!index)
                    firmwareUploadRequest = request;
                if (!requestAuthorised(request))
                    return;
                if (!uploadMayWrite(request, index, true))
                    return;
                firmwareUploadChunk(filename, index, data, len, final);
            });
    }

    // Like requireAuth, without answering: the upload's done handler does that.
    bool WebServer::requestAuthorised(AsyncWebServerRequest *request) const
    {
        const Config &cfg = getConfigCallback();
        return !cfg.auth.enabled || cfg.auth.password.empty() ||
               request->authenticate(cfg.auth.username.c_str(), cfg.auth.password.c_str());
    }

    void WebServer::handleFsUploadDone(AsyncWebServerRequest *request)
    {
        if (!requireAuth(request))
            return;
        const bool fsFailed = fsUpload.error;
        // A refused file (wrong size) never touched the filesystem: keep running.
        const bool restart = !fsFailed || fsUpload.touched;
        const String responseJson =
            fsFailed ? String(createErrorJson(fsUpload.errorMessage.c_str()).c_str()) : String("{\"success\":true}");
        fsUpload = FsUpload{}; // reset for the next upload
        sendClosing(request, fsFailed ? (restart ? 500 : 400) : 200, responseJson);
        // The filesystem was unmounted and rewritten: only a restart remounts it,
        // so a close never cancels this one (it waits while the roof moves).
        if (restart)
            WebServer::scheduleMandatoryRestart(1000);
    }

    void WebServer::handleFirmwareUploadDone(AsyncWebServerRequest *request)
    {
        if (!requireAuth(request))
            return;
        if (!uploadRefusal.empty())
        {
            sendClosing(request, 400, createErrorJson(uploadRefusal.c_str()).c_str());
            uploadRefusal.clear();
            return;
        }
        const bool success = !Update.hasError() || activatedOnRetry;
        String responseJson;
        if (success)
        {
            // `retried`: the boot switch only took on the second try (see
            // retryActivation), so the success rate of the first attempt can
            // be measured (SQMeter spec 012 SC-001).
            responseJson = activatedOnRetry ? "{\"success\":true,\"retried\":true}" : "{\"success\":true}";
        }
        else
        {
            const uint8_t error = Update.getError();
            const char *text = updateErrorText(error);
            const String message = text != nullptr ? String(text) : "Error code: " + String(error);
            responseJson = createErrorJson(message.c_str()).c_str();
        }
        sendClosing(request, success ? 200 : 500, responseJson);
        if (success)
            WebServer::scheduleRestart(1000);
    }

} // namespace SQM
