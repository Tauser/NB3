#pragma once

// Retain argument checking/use without producing device logs in host tests.
template <typename... Args>
inline void nb_test_log(const char*, const char*, Args...) {}
#define ESP_LOGI(...) nb_test_log(__VA_ARGS__)
