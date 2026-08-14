#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define URK_DEV_TEST_API_VERSION 1
#define URK_DEV_TEST_NAME_MAX 128
#define URK_DEV_TEST_SOURCE_MAX 260
#define URK_DEV_TEST_TAGS_MAX 256
#define URK_DEV_TEST_MESSAGE_MAX 2048
#define URK_DEV_TEST_DETAILS_MAX 4096

typedef struct URK_DevTestDescriptor {
    uint32_t version;
    uint32_t size;
    char name[URK_DEV_TEST_NAME_MAX];
    char sourceFile[URK_DEV_TEST_SOURCE_MAX];
    uint32_t sourceLine;
    char tags[URK_DEV_TEST_TAGS_MAX];
} URK_DevTestDescriptor;

typedef struct URK_DevTestResult {
    uint32_t version;
    uint32_t size;
    int passed;
    uint64_t durationMicroseconds;
    char message[URK_DEV_TEST_MESSAGE_MAX];
    char details[URK_DEV_TEST_DETAILS_MAX];
} URK_DevTestResult;

typedef uint32_t (*URK_DevTestCountFn)();
typedef int (*URK_DevTestDescribeFn)(uint32_t index, URK_DevTestDescriptor *descriptor);
typedef int (*URK_DevTestRunFn)(const char *name, URK_DevTestResult *result);

#ifdef __cplusplus
}
#endif
