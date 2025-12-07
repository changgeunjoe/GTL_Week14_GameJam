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

	for (UActorComponent* Component : Owner->GetOwnedComponents())
	{
		if (UParticleSystemComponent* ParticleComp = Cast<UParticleSystemComponent>(Component))
		{
			if (bActivate)
			{
				ParticleComp->ActivateSystem();
			}
			else
			{
				ParticleComp->DeactivateSystem();
			}
		}
	}
}
