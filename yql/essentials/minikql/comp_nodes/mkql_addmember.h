#pragma once
#include <yql/essentials/minikql/computation/mkql_computation_node.h>

namespace NKikimr {
namespace NMiniKQL {

IComputationNode* AddMember(const TComputationNodeFactoryContext& ctx, TRuntimeNode structData, TRuntimeNode memberData, TRuntimeNode indexData);

}
}
