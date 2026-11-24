#pragma once
#include <atomic>
#include <cstdint>
struct RuntimeBudget {std::atomic<bool> streaming{false},background{false};std::atomic<uint32_t> buffered{0},lastGlitch{0},glitches{0};bool allowNetwork(uint32_t)const{return true;}};
