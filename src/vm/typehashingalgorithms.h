// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

// ---------------------------------------------------------------------------
// Generic functions to compute the hashcode value of types
// ---------------------------------------------------------------------------

#pragma once
#include <stdlib.h>

// DIFFERENT FROM CORERT: Hashing code currently different from CoreRT

//
// Returns the hashcode value of the 'src' string
//
inline static DWORD ComputeNameHashCode(LPCUTF8 src)
{
    if (src == NULL || *src == '\0')
        return 0;

    DWORD hash1 = 0x6DA3B944;
    DWORD hash2 = 0;

    for (COUNT_T i = 0; src[i] != '\0'; i += 2)
    {
        hash1 = (hash1 + _rotl(hash1, 5)) ^ src[i];
        if (src[i + 1] != '\0')
            hash2 = (hash2 + _rotl(hash2, 5)) ^ src[i + 1];
        else
            break;
    }

    hash1 += _rotl(hash1, 8);
    hash2 += _rotl(hash2, 8);

    return hash1 ^ hash2;
}

inline static DWORD ComputeNameHashCode(LPCUTF8 pszNamespace, LPCUTF8 pszName)
{
    return ComputeNameHashCode(pszNamespace) ^ ComputeNameHashCode(pszName);
}

inline static DWORD ComputeArrayTypeHashCode(DWORD elementTypeHashcode, DWORD rank)
{
    // Arrays are treated as generic types in some parts of our system. The array hashcodes are 
    // carefully crafted to be the same as the hashcodes of their implementation generic types.

    int hashCode;
    if (rank == 1)
    {
        hashCode = 0xd5313557;
        _ASSERTE(hashCode == ComputeNameHashCode("System.Array`1"));
    }
    else
    {
        hashCode = ComputeNameHashCode("System.MDArray`1") ^ rank;
    }

    hashCode = (hashCode + _rotl(hashCode, 13)) ^ elementTypeHashcode;
    return (hashCode + _rotl(hashCode, 15));
}

inline static DWORD ComputePointerTypeHashCode(DWORD pointeeTypeHashcode)
{
    return (pointeeTypeHashcode + _rotl(pointeeTypeHashcode, 5)) ^ 0x12D0;
}

inline static DWORD ComputeByrefTypeHashCode(DWORD parameterTypeHashcode)
{
    return (parameterTypeHashcode + _rotl(parameterTypeHashcode, 7)) ^ 0x4C85;
}

inline static DWORD ComputeNestedTypeHashCode(DWORD enclosingTypeHashcode, DWORD nestedTypeNameHash)
{
    return (enclosingTypeHashcode + _rotl(enclosingTypeHashcode, 11)) ^ nestedTypeNameHash;
}

template <typename TA, typename TB>
inline static DWORD ComputeGenericInstanceHashCode(DWORD definitionHashcode, DWORD arity, const TA& genericTypeArguments, DWORD (*getHashCode)(TB))
{
    DWORD hashcode = definitionHashcode;
    for (DWORD i = 0; i < arity; i++)
    {
        DWORD argumentHashCode = getHashCode(genericTypeArguments[i]);
        hashcode = (hashcode + _rotl(hashcode, 13)) ^ argumentHashCode;
    }
    return (hashcode + _rotl(hashcode, 15));
}
