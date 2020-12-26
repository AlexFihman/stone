#include "square.h"

using namespace std;

square::square(size_t asize)
{
    size = asize;
    size_t arraysize = (size * size + 63) / 64;

    rawdata = new uint64_t[arraysize];
    for (uint64_t i=0;i<arraysize;i++)
        rawdata[i] = 0;
}

square::~square()
{
    delete[] rawdata;
}

bool square::getValue(const int x, const int y)
{
    size_t pos = x * size + y;
    uint64_t result = (rawdata[pos/64] >> (pos%64)) & 0x01;
    return result;
}
void square::setValue(const int x, const int y, const bool value)
{
    size_t pos = x * size + y;
    if (value)
        rawdata[pos/64] |= 1ULL << (pos % 64);
    else
        rawdata[pos/64] &= ~(1ULL << (pos % 64));
}
