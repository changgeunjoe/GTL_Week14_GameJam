#include "pch.h"
#include "AnimNotify_ParticleOnOff.h"
#include "Source/Runtime/Engine/Components/SkeletalMeshComponent.h"
#include "Source/Runtime/Engine/Components/ParticleSystemComponent.h"
#include "Source/Runtime/Core/Object/Actor.h"

IMPLEMENT_CLASS(UAnimNotify_ParticleOnOff)

UAnimNotify_ParticleOnOff::UAnimNotify_ParticleOnOff()
{
	bActivate = true;
}

void UAnimNotify_ParticleOnOff::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation)
{
	Super::Notify(MeshComp, Animation);

	if (!MeshComp)
	{
		return;
	}

	AActor* Owner = MeshComp->GetOwner();
	if (!Owner)
	{
		return;
	}

	bool bAll = ParticleName.empty() || ParticleName == "All";

	for (UActorComponent* Component : Owner->GetOwnedComponents())
	{
		if (UParticleSystemComponent* ParticleComp = Cast<UParticleSystemComponent>(Component))
		{
			bool bMatch = false;
			if (bAll)
			{
				bMatch = true;
			}
			else
			{
				FString CompName = ParticleComp->GetParticleName();
				if (CompName.empty())
				{
					CompName = ParticleComp->GetName();
				}

				if (CompName.find(ParticleName) != FString::npos)
				{
					bMatch = true;
				}
			}

			if (bMatch)
			{
				if (bActivate)
				{
					// 파티클 완전 활성화: Tick 켜기 + Spawning 재개
					ParticleComp->ActivateSystem();
				}
				else
				{
					// 스폰만 중지 (렌더링은 유지)
					ParticleComp->PauseSpawning();
				}
			}
		}
	}
}
