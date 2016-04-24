// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#include "common.h"
#include "stablehashcode.h"
#include "typehashingalgorithms.h"

// DIFFERENT FROM CORERT: Hashing code currently different from CoreRT

DWORD GetVersionResilientTypeHashCode(IMDInternalImport *pMDImport, mdExportedType token)
{
    _ASSERTE(TypeFromToken(token) == mdtTypeDef ||
        TypeFromToken(token) == mdtTypeRef ||
        TypeFromToken(token) == mdtExportedType);
    _ASSERTE(!IsNilToken(token));

    LPCUTF8 szNamespace;
    LPCUTF8 szName;
    DWORD hashcode = 0;

    while (true)
    {
        if (IsNilToken(token))
            return hashcode;

        switch (TypeFromToken(token))
        {
        case mdtTypeDef:
            if (FAILED(pMDImport->GetNameOfTypeDef(token, &szName, &szNamespace)))
                ThrowHR(COR_E_BADIMAGEFORMAT);
            if (FAILED(pMDImport->GetNestedClassProps(token, &token)))
                token = mdTokenNil;
            break;

        case mdtTypeRef:
            if (FAILED(pMDImport->GetNameOfTypeRef(token, &szNamespace, &szName)))
                ThrowHR(COR_E_BADIMAGEFORMAT);
            if (FAILED(pMDImport->GetResolutionScopeOfTypeRef(token, &token)))
                token = mdTokenNil;
            break;

        case mdtExportedType:
            if (FAILED(pMDImport->GetExportedTypeProps(token, &szNamespace, &szName, &token, NULL, NULL)))
                ThrowHR(COR_E_BADIMAGEFORMAT);
            break;

        default:
            return hashcode;
        }

        hashcode ^= ComputeNameHashCode(szNamespace, szName);
    }
}

#ifndef DACCESS_COMPILE
DWORD GetVersionResilientTypeHashCode(TypeHandle type)
{
    if (!type.IsTypeDesc())
    {
        MethodTable *pMT = type.AsMethodTable();

        DWORD hashcode = 0;
        if (pMT->IsArray())
        {
            return ComputeArrayTypeHashCode(GetVersionResilientTypeHashCode(pMT->GetApproxArrayElementTypeHandle()), pMT->GetRank());
        }
        else if (!IsNilToken(pMT->GetCl()))
        {
            LPCUTF8 szNamespace;
            LPCUTF8 szName;
            IfFailThrow(pMT->GetMDImport()->GetNameOfTypeDef(pMT->GetCl(), &szName, &szNamespace));
            hashcode ^= ComputeNameHashCode(szNamespace, szName);
        }

        MethodTable *pMTEnclosing = pMT->LoadEnclosingMethodTable(CLASS_LOAD_UNRESTOREDTYPEKEY);
        if (pMTEnclosing != NULL)
        {
            hashcode = ComputeNestedTypeHashCode(GetVersionResilientTypeHashCode(TypeHandle(pMTEnclosing)), hashcode);
        }

        if (!pMT->IsGenericTypeDefinition() && pMT->HasInstantiation())
        {
            return ComputeGenericInstanceHashCode(hashcode,
                pMT->GetInstantiation().GetNumArgs(), pMT->GetInstantiation(), GetVersionResilientTypeHashCode);
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
        StackScratchBuffer buffer;
        return ComputeNameHashCode(name.GetUTF8(buffer));
    }
    else
    if (type.IsArray())
    {
        ArrayTypeDesc *pArray = type.AsArray();
        return ComputeArrayTypeHashCode(GetVersionResilientTypeHashCode(pArray->GetArrayElementTypeHandle()), pArray->GetRank());
    }
    else
    if (type.IsPointer())
    {
        return ComputePointerTypeHashCode(GetVersionResilientTypeHashCode(type.AsTypeDesc()->GetTypeParam()));
    }
    else
    if (type.IsByRef())
    {
        return ComputeByrefTypeHashCode(GetVersionResilientTypeHashCode(type.AsTypeDesc()->GetTypeParam()));
    }

    assert(false);
    return 0;
}

DWORD GetVersionResilientMethodHashCode(MethodDesc *pMD)
{
    DWORD hashCode = GetVersionResilientTypeHashCode(TypeHandle(pMD->GetMethodTable()));

    // Todo: Add signature to hash.
    if (pMD->GetNumGenericMethodArgs() > 0)
    {
        hashCode ^= ComputeGenericInstanceHashCode(ComputeNameHashCode(pMD->GetName()), pMD->GetNumGenericMethodArgs(), pMD->GetMethodInstantiation(), GetVersionResilientTypeHashCode);
    }
    else
    {
        hashCode ^= ComputeNameHashCode(pMD->GetName());
    }

    return hashCode;
}
#endif // DACCESS_COMPILE
