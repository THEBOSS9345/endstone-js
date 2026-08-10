/* Links libnode and calls into it through the N-API C surface only.
 *
 * Deliberately C: it proves the package is usable without agreeing with libnode's C++ runtime, which
 * is the whole premise of the ABI firewall in node/. napi_get_last_error_info needs no environment,
 * so this exercises real linkage and loading without starting an isolate. */

#include <stdio.h>

#include <node_api.h>

int main(void)
{
    const napi_extended_error_info *info = NULL;
    /* A null env is rejected, which is fine - reaching the check means libnode resolved and loaded. */
    const napi_status status = napi_get_last_error_info(NULL, &info);
    printf("napi_get_last_error_info returned %d\n", (int)status);
    return 0;
}
