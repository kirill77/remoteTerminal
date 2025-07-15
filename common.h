#pragma once

// Common protocol definitions for remote terminal client/server

// Protocol constants
#define DEFAULT_PORT "27015"
#define DEFAULT_BUFLEN 4096

// End-of-response marker for message delimiting
#define END_OF_RESPONSE_MARKER "<<END_OF_RESPONSE>>" 