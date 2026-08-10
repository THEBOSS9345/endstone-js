/*
 * Copyright (c) 2026, The Endstone Project. (https://endstone.dev) All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Exercises the Node host outside BDS. Deliberately compiled as C: if this links and runs, the
 * boundary really is C-only.
 */

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#define ESN_SLEEP_MS(ms) Sleep((DWORD)(ms))
#else
#include <time.h>
static void esn_sleep_ms(long ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
#define ESN_SLEEP_MS(ms) esn_sleep_ms((long)(ms))
#endif

#include "endstone_node_abi.h"

/* One pump per simulated BDS tick, 20 ticks per second. */
#define ESN_TICK_MS 50
#define ESN_TICKS 60

static const char *kLevelNames[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL"};

static void ESN_CALL on_log(void *user_data, int level, const char *message, size_t length)
{
    const char *name = (level >= 0 && level <= 5) ? kLevelNames[level] : "INFO";
    (void)user_data;
    printf("[%-8s] %.*s\n", name, (int)length, message);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    esn_host_config config;
    esn_host *host = NULL;
    esn_status status;
    int exit_code = 0;
    int more_work = 0;
    int i;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <script.js>\n", argv[0]);
        return 2;
    }

    printf("host ABI version: %u (header %u)\n", esn_abi_version(), ESN_ABI_VERSION);
    if (esn_abi_version() != ESN_ABI_VERSION) {
        fprintf(stderr, "ABI mismatch\n");
        return 3;
    }

    memset(&config, 0, sizeof(config));
    config.abi_version = ESN_ABI_VERSION;
    config.log = on_log;
    config.log_user_data = NULL;
    config.script_path = argv[1];
    config.exec_path = argv[0];

    status = esn_host_create(&config, &host);
    if (status != ESN_OK) {
        fprintf(stderr, "esn_host_create failed: %s\n", esn_status_message(status));
        return 4;
    }
    printf("-- created --\n");

    status = esn_host_start(host);
    if (status != ESN_OK) {
        fprintf(stderr, "esn_host_start failed: %s\n", esn_status_message(status));
        esn_host_destroy(host, &exit_code);
        return 5;
    }
    printf("-- started --\n");

    for (i = 0; i < ESN_TICKS; ++i) {
        status = esn_host_pump(host, &more_work);
        if (status != ESN_OK) {
            fprintf(stderr, "esn_host_pump failed: %s\n", esn_status_message(status));
            break;
        }
        ESN_SLEEP_MS(ESN_TICK_MS);
    }
    printf("-- pumped %d ticks at %dms, more_work=%d --\n", ESN_TICKS, ESN_TICK_MS, more_work);

    status = esn_host_destroy(host, &exit_code);
    printf("-- destroyed: %s, exit_code=%d --\n", esn_status_message(status), exit_code);
    return 0;
}
