#ifndef SQUARE_H
#define SQUARE_H

#include <cstddef>
#include <iostream>
#include <array>

using namespace std;

class square
{
    public:
        bool getValue(const int x, const int y);
        void setValue(const int x, const int y, const bool value);
        square(size_t size);
        virtual ~square();

    protected:

    private:
        size_t size;
        uint64_t* rawdata;
};

#endif // SQARE_H
