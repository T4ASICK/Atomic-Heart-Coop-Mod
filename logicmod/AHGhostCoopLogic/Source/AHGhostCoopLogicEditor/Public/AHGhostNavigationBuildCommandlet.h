#pragma once
#include "Commandlets/Commandlet.h"
#include "AHGhostNavigationBuildCommandlet.generated.h"

UCLASS()
class UAHGhostNavigationBuildCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UAHGhostNavigationBuildCommandlet();
    virtual int32 Main(const FString& Params) override;
};
