// Copyright (c) 2026 THEBOSS9345 (https://github.com/THEBOSS9345/endstone-js)
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "endstone_node_abi.h"

namespace endstone::node {

/**
 * @brief The Node host's C entry points, resolved by name at runtime.
 *
 * Resolved rather than linked so a missing or mismatched host degrades to a log line instead of a
 * plugin that refuses to load.
 */
struct HostApi {
    uint32_t(ESN_CALL *abi_version)(void){nullptr};
    const char *(ESN_CALL *status_message)(esn_status){nullptr};
    esn_status(ESN_CALL *create)(const esn_host_config *, esn_host **){nullptr};
    esn_status(ESN_CALL *start)(esn_host *){nullptr};
    esn_status(ESN_CALL *pump)(esn_host *, int *){nullptr};
    esn_status(ESN_CALL *destroy)(esn_host *, int *){nullptr};
    esn_status(ESN_CALL *load_plugin)(esn_host *, const char *, esn_plugin **){nullptr};
    esn_status(ESN_CALL *plugin_get_meta)(esn_plugin *, esn_plugin_meta *){nullptr};
    esn_status(ESN_CALL *plugin_invoke)(esn_plugin *, esn_plugin_hook){nullptr};
    esn_status(ESN_CALL *plugin_unload)(esn_plugin *){nullptr};
    esn_status(ESN_CALL *plugin_reload)(esn_plugin *){nullptr};
    esn_status(ESN_CALL *plugin_command)(esn_plugin *, const char *, esn_handle, const char *const *, size_t,
                                        int *){nullptr};
    esn_status(ESN_CALL *dispatch_event)(esn_host *, uint32_t, esn_handle){nullptr};
    esn_status(ESN_CALL *run_task)(esn_host *, uint32_t){nullptr};
    esn_status(ESN_CALL *render_map)(esn_host *, uint32_t, esn_handle, esn_handle){nullptr};
    esn_status(ESN_CALL *form_result)(esn_host *, uint32_t, int, const char *, size_t){nullptr};

    [[nodiscard]] const char *message(esn_status status) const
    {
        return status_message ? status_message(status) : "?";
    }
};

}  // namespace endstone::node
