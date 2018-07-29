/*
 * Copyright 2016-2018 Alex Beregszaszi et al.
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

#ifndef __HERA_H
#define __HERA_H

#include <evmc/evmc.h>

#if defined _MSC_VER || defined __MINGW32__
# define HERA_EXPORT __declspec(dllexport)
# define HERA_IMPORT __declspec(dllimport)
#elif __GNU__ >= 4
# define HERA_EXPORT __attribute__((visibility("default")))
# define HERA_IMPORT __attribute__((visibility("default")))
#else
# define HERA_EXPORT
# define HERA_IMPORT
#endif

#if __cplusplus
extern "C" {
#endif

HERA_EXPORT
struct evmc_instance* evmc_create_hera(void);

#if __cplusplus
}
#endif

#endif
