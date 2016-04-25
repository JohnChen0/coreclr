// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

// ---------------------------------------------------------------------------
// Generic functions to compute the hashcode value of types
// ---------------------------------------------------------------------------

#pragma once
#include <stdlib.h>

//
// Returns the hashcode value of the 'src' string
//
inline static int ComputeNameHashCode(const SString &src)
{
    int hash1 = 0x6DA3B944;
    int hash2 = 0;

    for (COUNT_T i = 0; i < src.GetCount(); i += 2)
    {
        hash1 = (hash1 + _rotl(hash1, 5)) ^ src[i];
        if ((i + 1) < src.GetCount())
            hash2 = (hash2 + _rotl(hash2, 5)) ^ src[i + 1];
    }

    hash1 += _rotl(hash1, 8);
    hash2 += _rotl(hash2, 8);

    return hash1 ^ hash2;
}

inline static int ComputeArrayTypeHashCode(int elementTypeHashcode, int rank)
{
    // Arrays are treated as generic types in some parts of our system. The array hashcodes are 
    // carefully crafted to be the same as the hashcodes of their implementation generic types.

    int hashCode;
    if (rank == 1)
    {
        hashCode = 0xd5313557;
        _ASSERTE(hashCode == ComputeNameHashCode(W("System.Array`1")));
    }
    else
    {
        LPCWSTR typeName;
        switch (rank)
        {
        case 2: typeName = W("System.MDArrayRank2`1");
        case 3: typeName = W("System.MDArrayRank3`1");
        default: _ASSERTE(!"NYI - MD arrays with rank > 3"); typeName = NULL;
        }
        hashCode = ComputeNameHashCode(typeName);
    }

    hashCode = (hashCode + _rotl(hashCode, 13)) ^ elementTypeHashcode;
    return (hashCode + _rotl(hashCode, 15));
}

inline static int ComputePointerTypeHashCode(int pointeeTypeHashcode)
{
    return (pointeeTypeHashcode + _rotl(pointeeTypeHashcode, 5)) ^ 0x12D0;
}

inline static int ComputeByrefTypeHashCode(int parameterTypeHashcode)
{
    return (parameterTypeHashcode + _rotl(parameterTypeHashcode, 7)) ^ 0x4C85;
}

inline static int ComputeNestedTypeHashCode(int enclosingTypeHashcode, int nestedTypeNameHash)
{
    return (enclosingTypeHashcode + _rotl(enclosingTypeHashcode, 11)) ^ nestedTypeNameHash;
}

template <typename TA, typename TB>
inline static int ComputeGenericInstanceHashCode(int definitionHashcode, int arity, const TA& genericTypeArguments, int (*getHashCode)(TB))
{
    int hashcode = definitionHashcode;
    for (int i = 0; i < arity; i++)
    {
        int argumentHashCode = getHashCode(genericTypeArguments[i]);
        hashcode = (hashcode + _rotl(hashcode, 13)) ^ argumentHashCode;
    }
    return (hashcode + _rotl(hashcode, 15));
}
