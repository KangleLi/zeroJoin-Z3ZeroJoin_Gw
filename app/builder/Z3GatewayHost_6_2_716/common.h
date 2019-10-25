
#ifndef CARELSDK_COMMON_H_
#define CARELSDK_COMMON_H_

#include "app/framework/include/af.h"
#include "list.h"

#define _debug(...) { \
	emberAfDebugPrint("[--debug--]file: %s, func: %s, line: %d\n# ", __FILE__, __func__, __LINE__); \
	emberAfDebugPrintln(__VA_ARGS__); \
}


#endif /* CARELSDK_COMMON_H_ */
