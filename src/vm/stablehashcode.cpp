// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#include "common.h"
#include "stablehashcode.h"
#include "typehashingalgorithms.h"

int GetStableHashCode(TypeHandle type)
{
    if (!type.IsTypeDesc())
    {
        MethodTable *pMT = type.AsMethodTable();

        StackSString name;
        pMT->_GetFullyQualifiedNameForClass(name);

        unsigned hashcode = ComputeNameHashCode(name);

        MethodTable *pMTEnclosing = pMT->LoadEnclosingMethodTable(CLASS_LOAD_UNRESTOREDTYPEKEY);
        if (pMTEnclosing != NULL)
        {
            hashcode = ComputeNestedTypeHashCode(GetStableHashCode(TypeHandle(pMTEnclosing)), hashcode);
        }

        if (!pMT->IsGenericTypeDefinition() && pMT->HasInstantiation())
        {
            return ComputeGenericInstanceHashCode(hashcode,
                pMT->GetInstantiation().GetNumArgs(), pMT->GetInstantiation(), GetStableHashCode);
        }
        else
        {
            return hashcode;
        }
    }
    else
    if (type.IsGenericVariable())
    {
        StackSString name;
        type.GetName(name);
        return ComputeNameHashCode(name);
    }
    else
    if (type.IsArray())
    {
        ArrayTypeDesc *pArray = type.AsArray();
        return ComputeArrayTypeHashCode(GetStableHashCode(pArray->GetArrayElementTypeHandle()), pArray->GetRank());
    }
    else
    if (type.IsPointer())
    {
        return ComputePointerTypeHashCode(GetStableHashCode(type.AsTypeDesc()->GetTypeParam()));
    }
    else
    if (type.IsByRef())
    {
        return ComputeByrefTypeHashCode(GetStableHashCode(type.AsTypeDesc()->GetTypeParam()));
    }

    assert(false);
    return 0;
}

int GetStableMethodHashCode(MethodDesc *pMD)
{
    int hashCode = GetStableHashCode(TypeHandle(pMD->GetMethodTable()));

    // Todo: Add signature to hash.
    if (pMD->GetNumGenericMethodArgs() > 0)
    {
        hashCode ^= ComputeGenericInstanceHashCode(ComputeNameHashCode(SString(SString::Utf8, pMD->GetName())), pMD->GetNumGenericMethodArgs(), pMD->GetMethodInstantiation(), GetStableHashCode);
    }
    else
    {
        hashCode ^= ComputeNameHashCode(SString(SString::Utf8, pMD->GetName())); // and sig
    }

    return hashCode;
}
