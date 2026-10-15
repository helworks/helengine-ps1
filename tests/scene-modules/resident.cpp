extern "C" {
volatile int ResidentLive = 0;
void ResidentNote(int delta) { ResidentLive = ResidentLive + delta; }
int ResidentScale(int value) { return value * 3; }
void _start() { ResidentNote(0); }
}
