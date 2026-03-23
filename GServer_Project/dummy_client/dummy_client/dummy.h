#pragma once
#include <atomic>

void InitializeNetwork();
void GetPointCloud(int* size, float** points);

extern std::atomic_int global_delay;
extern std::atomic_int active_clients;
