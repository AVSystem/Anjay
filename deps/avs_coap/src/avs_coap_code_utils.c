/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

#include <avs_coap_init.h>

#include <avsystem/commons/avs_utils.h>

#include <avsystem/coap/code.h>

#define MODULE_NAME coap_code
#include <avs_coap_x_log_config.h>

#include "avs_coap_code_utils.h"

VISIBILITY_SOURCE_BEGIN

const char *avs_coap_code_to_string(uint8_t code, char *buf, size_t buf_size) {
    static const struct {
        uint8_t code;
        const char *name;
    } CODE_NAMES[] = {
        // clang-format off
        { AVS_COAP_CODE_EMPTY,                      "Empty"                      },

        { AVS_COAP_CODE_GET,                        "Get"                        },
        { AVS_COAP_CODE_POST,                       "Post"                       },
        { AVS_COAP_CODE_PUT,                        "Put"                        },
        { AVS_COAP_CODE_DELETE,                     "Delete"                     },
        { AVS_COAP_CODE_FETCH,                      "Fetch"                      },
        { AVS_COAP_CODE_PATCH,                      "Patch"                      },
        { AVS_COAP_CODE_IPATCH,                     "iPatch"                     },

        { AVS_COAP_CODE_CREATED,                    "Created"                    },
        { AVS_COAP_CODE_DELETED,                    "Deleted"                    },
        { AVS_COAP_CODE_VALID,                      "Valid"                      },
        { AVS_COAP_CODE_CHANGED,                    "Changed"                    },
        { AVS_COAP_CODE_CONTENT,                    "Content"                    },
        { AVS_COAP_CODE_CONTINUE,                   "Continue"                   },

        { AVS_COAP_CODE_BAD_REQUEST,                "Bad Request"                },
        { AVS_COAP_CODE_UNAUTHORIZED,               "Unauthorized"               },
        { AVS_COAP_CODE_BAD_OPTION,                 "Bad Option"                 },
        { AVS_COAP_CODE_FORBIDDEN,                  "Forbidden"                  },
        { AVS_COAP_CODE_NOT_FOUND,                  "Not Found"                  },
        { AVS_COAP_CODE_METHOD_NOT_ALLOWED,         "Method Not Allowed"         },
        { AVS_COAP_CODE_NOT_ACCEPTABLE,             "Not Acceptable"             },
        { AVS_COAP_CODE_REQUEST_ENTITY_INCOMPLETE,  "Request Entity Incomplete"  },
        { AVS_COAP_CODE_PRECONDITION_FAILED,        "Precondition Failed"        },
        { AVS_COAP_CODE_REQUEST_ENTITY_TOO_LARGE,   "Entity Too Large"           },
        { AVS_COAP_CODE_UNSUPPORTED_CONTENT_FORMAT, "Unsupported Content Format" },

        { AVS_COAP_CODE_INTERNAL_SERVER_ERROR,      "Internal Server Error"      },
        { AVS_COAP_CODE_NOT_IMPLEMENTED,            "Not Implemented"            },
        { AVS_COAP_CODE_BAD_GATEWAY,                "Bad Gateway"                },
        { AVS_COAP_CODE_SERVICE_UNAVAILABLE,        "Service Unavailable"        },
        { AVS_COAP_CODE_GATEWAY_TIMEOUT,            "Gateway Timeout"            },
        { AVS_COAP_CODE_PROXYING_NOT_SUPPORTED,     "Proxying Not Supported"     },
        // clang-format on
    };

    const char *name = "unknown";
    for (size_t i = 0; i < AVS_ARRAY_SIZE(CODE_NAMES); ++i) {
        if (CODE_NAMES[i].code == code) {
            name = CODE_NAMES[i].name;
            break;
        }
    }

    if (avs_simple_snprintf(buf, buf_size, "%u.%02u %s",
                            avs_coap_code_get_class(code),
                            avs_coap_code_get_detail(code), name)
            < 0) {
        AVS_UNREACHABLE("buffer too small for CoAP msg code string");
        return "<error>";
    }

    return buf;
}

uint8_t avs_coap_code_get_class(uint8_t code) {
    return (uint8_t) _AVS_FIELD_GET(code, _AVS_COAP_CODE_CLASS_MASK,
                                    _AVS_COAP_CODE_CLASS_SHIFT);
}

uint8_t avs_coap_code_get_detail(uint8_t code) {
    return (uint8_t) _AVS_FIELD_GET(code, _AVS_COAP_CODE_DETAIL_MASK,
                                    _AVS_COAP_CODE_DETAIL_SHIFT);
}

bool avs_coap_code_is_client_error(uint8_t code) {
    return avs_coap_code_get_class(code) == 4;
}

bool avs_coap_code_is_server_error(uint8_t code) {
    return avs_coap_code_get_class(code) == 5;
}

bool avs_coap_code_is_success(uint8_t code) {
    return avs_coap_code_get_class(code) == 2;
}

bool avs_coap_code_is_request(uint8_t code) {
    return avs_coap_code_get_class(code) == 0
           && avs_coap_code_get_detail(code) > 0;
}

bool avs_coap_code_is_response(uint8_t code) {
    return avs_coap_code_is_success(code) || avs_coap_code_is_client_error(code)
           || avs_coap_code_is_server_error(code);
}
