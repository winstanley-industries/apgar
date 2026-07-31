int main() {
  volatile int maximum = 2147483647;
  volatile int one = 1;
  volatile int overflow = maximum + one;
  (void)overflow;
  __builtin_printf("P4PAIR-UBSAN-LIVE-PROBE-SENTINEL\n");
  return 0;
}
