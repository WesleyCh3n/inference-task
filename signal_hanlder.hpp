#pragma once

#include <csignal>
#include <sys/signal.h>

extern volatile sig_atomic_t g_stop;

void sigint_handler(int);
void register_signal_handler();
struct sigaction get_stop_action();

void sigint_handler(int) { g_stop = 1; }

void register_signal_handler() {
  struct sigaction sig_stop_act = get_stop_action();
  sigaction(SIGINT, &sig_stop_act, nullptr);
  sigaction(SIGTERM, &sig_stop_act, nullptr);
}

struct sigaction get_stop_action() {
  struct sigaction sig_stop_act;
  sig_stop_act.sa_handler = sigint_handler;
  sigemptyset(&sig_stop_act.sa_mask);
  sig_stop_act.sa_flags = 0;
  return sig_stop_act;
}
