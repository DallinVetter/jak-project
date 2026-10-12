// Desktop services the game runtime calls but a PS5 title does not have: Discord
// rich presence, HTTP fetches through libcurl, and native message boxes. Each entry
// point keeps its desktop signature and reports "unavailable" the way the real
// library would, so game/ and common/ need no PS5 branches for them.
#include "common/log/log.h"

#include "curl/curl.h"
#include "third-party/discord-rpc/include/discord_rpc.h"

extern "C" {
#include "third-party/libtinyfiledialogs/tinyfiledialogs.h"
}

void Discord_Initialize(const char*, DiscordEventHandlers*, int, const char*) {}
void Discord_Shutdown(void) {}
void Discord_UpdatePresence(const DiscordRichPresence*) {}
void Discord_ClearPresence(void) {}

CURLcode curl_global_init(long) {
  return CURLE_FAILED_INIT;
}
void curl_global_cleanup(void) {}
CURL* curl_easy_init(void) {
  return nullptr;
}
void curl_easy_cleanup(CURL*) {}
CURLcode curl_easy_perform(CURL*) {
  return CURLE_FAILED_INIT;
}
#undef curl_easy_setopt
CURLcode curl_easy_setopt(CURL*, CURLoption, ...) {
  return CURLE_FAILED_INIT;
}
const char* curl_easy_strerror(CURLcode) {
  return "HTTP is not available in the PS5 build";
}

// There is no dialog to show: a fatal startup message goes to the log, where the
// hardware cycle collects it.
int tinyfd_messageBox(char const* const title,
                      char const* const message,
                      char const* const,
                      char const* const,
                      int const) {
  lg::error("[ps5] dialog: {}: {}", title ? title : "", message ? message : "");
  return 1;
}
