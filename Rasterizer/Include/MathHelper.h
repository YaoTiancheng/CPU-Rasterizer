#pragma once

namespace MathHelper
{
    // Divides two integers and rounds up
    template <typename T>
    T DivideAndRoundUp( T Dividend, T Divisor )
    {
        return ( Dividend + Divisor - 1 ) / Divisor;
    }
}