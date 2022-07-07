// Fill out your copyright notice in the Description page of Project Settings.


#include "AutomataDriver.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Misc/Char.h"
#include "Async/Async.h"
#include "Async/AsyncWork.h"


// Sets default values
AAutomataDriver::AAutomataDriver()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateOptionalDefaultSubobject<USceneComponent>(TEXT("Root Component"));
}

void AAutomataDriver::PreInitializeComponents()
{
	Super::PreInitializeComponents();

	NumClusters = XClusters * ZClusters;
	CellsPerCluster = XCellsPerCluster * ZCellsPerCluster;
	NumCells = NumClusters * CellsPerCluster;
	NumCustomData = CellsPerCluster;

	// Create material and set up properties
	InitializeMaterial();

	//Set up InstanceComponents
	InitializeInstances();
	
}

void AAutomataDriver::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	InitializeCellRules();

	InitializeCellStates();

	InitializeTransforms();

	InitializeCellCustomData();

	InitializeCellNeighborhoods();

	InitializeCellNeighborsOf();

	InitializeCellProcessors();

	StartingDataSetup();
}


void AAutomataDriver::StartingDataSetup()
{
	NextStepTime = 0;

	RunProcessesOnce();

}

void AAutomataDriver::RunProcessesOnce()
{

	// start the processes (to calculate Next Step for all the cells)
	CurrentProcess = 0;
	Processors[CurrentProcess]->StartSynchronousTask();
	// having kicked off the first process, they should all cascade to completion, until the final one is complete
	// wait until the last process in this cascade is complete
	Processors.Last()->EnsureCompletion(false);
}

// Called when the game starts or when spawned
void AAutomataDriver::BeginPlay()
{
	Super::BeginPlay();

	StepComplete();
		
	// we are ready to start the iteration steps.
	GetWorldTimerManager().SetTimer(StepTimer, this, &AAutomataDriver::TimerFired, StepPeriod, true);
}

void AAutomataDriver::InitializeMaterial()
{

	// Create material and set up properties
	DynMaterial = UMaterialInstanceDynamic::Create(Mat, this);

	DynMaterial->SetScalarParameterValue("XCellsPerCluster", XCellsPerCluster);
	DynMaterial->SetScalarParameterValue("ZCellsPerCluster", ZCellsPerCluster);
	DynMaterial->SetScalarParameterValue("PhaseExponent", PhaseExponent);
	DynMaterial->SetScalarParameterValue("EmissiveMultiplier", EmissiveMultiplier);
	DynMaterial->SetVectorParameterValue("OnColor", OnColor);
	DynMaterial->SetVectorParameterValue("OffColor", OffColor);
	DynMaterial->SetScalarParameterValue("FadePerSecond", 1 / (StepPeriod * StepsToFade));
}

void AAutomataDriver::InitializeInstances()
{
	ClusterInstances.Reserve(Divisions);
	for (int i = 0; i < Divisions; ++i)
	{
		UInstancedStaticMeshComponent* NewClusterInstance = NewObject<UInstancedStaticMeshComponent>(this);
		NewClusterInstance->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		NewClusterInstance->RegisterComponent();

		NewClusterInstance->SetStaticMesh(Mesh);
		NewClusterInstance->SetMaterial(0, DynMaterial);

		// Engine-performance handling
		NewClusterInstance->SetMobility(EComponentMobility::Static);
		NewClusterInstance->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		NewClusterInstance->SetComponentTickEnabled(false);
	
		
		NewClusterInstance->NumCustomDataFloats = NumCustomData;

		ClusterInstances.Add(NewClusterInstance);
		AddInstanceComponent(NewClusterInstance);
	}
}


void AAutomataDriver::InitializeTransforms()
{

	TArray<FTransform> Transforms;
	Transforms.Init(FTransform(), NumClusters);

	// calculate transforms for each cluster
	ParallelFor(NumClusters, [&](int ClusterID)
	{
		// derive grid coordinates from cluster index
		int ClusterX = ClusterID % XClusters;
		int ClusterZ = ClusterID / XClusters;

		//Instance's transform is based on its grid coordinate
		Transforms[ClusterID] = FTransform((FVector(ClusterX, 0, ClusterZ) * Offset));
	});

	// the last ClusterInstance component may have fewer cells assigned to it
	int MaxClustersPerInstance = int(ceilf(float(NumClusters) / float(Divisions)));

	// Add instances to each ClusterInstance component, applying appropriate transform
	for (int ClusterID = 0; ClusterID < NumClusters; ++ClusterID)
	{
		int InstanceIndex = ClusterID / MaxClustersPerInstance;
		ClusterInstances[InstanceIndex]->AddInstance(Transforms[ClusterID]);
	}

}

void AAutomataDriver::InitializeCellRules()
{
	BirthRules.Init(false, 10);
	SurviveRules.Init(false, 10);

	for (TCHAR character : BirthString)
	{
		if (TChar<TCHAR>::IsDigit(character))
		{
			//BirthRules.Add(TChar<TCHAR>::ConvertCharDigitToInt(character));
			int32 index = TChar<TCHAR>::ConvertCharDigitToInt(character);
			BirthRules[index] = true;
		}
	}

	for (TCHAR character : SurviveString)
	{
		if (TChar<TCHAR>::IsDigit(character))
		{
			//SurviveRules.Add(TChar<TCHAR>::ConvertCharDigitToInt(character));
			int32 index = TChar<TCHAR>::ConvertCharDigitToInt(character);
			SurviveRules[index] = true;
		}
	}

}

void AAutomataDriver::InitializeCellStates()
{
	CurrentStates.Reserve(NumCells);

	for (int i = 0; i < NumCells; ++i)
	{
		CurrentStates.Add(FMath::FRandRange(0, TNumericLimits<int32>::Max() - 1) < Probability * TNumericLimits<int32>::Max());
	}

	NextStates.Init(false, NumCells);

	ChangedLastStep.Init(true, NumCells);
	ChangedThisStep.Init(true, NumCells);
}

void AAutomataDriver::InitializeCellCustomData()
{

	CurrentDataSlots.Init(nullptr, NumCells);
	NextDataSlots.Init(nullptr, NumCells);
	SwitchTimeSlots.Init(nullptr, NumCells);

	int MaxClustersPerInstance = int(ceilf(float(NumClusters) / float(Divisions)));


	ParallelFor(NumClusters, [&](int32 ClusterID)
	{
		// Determine which instance collection cluster belongs to
		int InstanceIndex = ClusterID / MaxClustersPerInstance;

		// define cluster ID local to the cluster's instance collection
		int InstanceClusterID = ClusterID - InstanceIndex * MaxClustersPerInstance;


		TArray<int> CellIDs = CellsIDsFromCluster(ClusterID);

		// for each cell within the cluster
		for (int Quadrant = 0; Quadrant < CellIDs.Num(); ++Quadrant)
		{

			int CurrentDataIndex = NumCustomData * InstanceClusterID + Quadrant;

			int CellID = CellIDs[Quadrant];

			CurrentDataSlots[CellID] = &(ClusterInstances[InstanceIndex]->PerInstanceSMCustomData[CurrentDataIndex]);

			*(CurrentDataSlots[CellID]) = -2 * (StepPeriod * StepsToFade);
		}
	});
}

void AAutomataDriver::InitializeCellNeighborhoods()
{
	Neighborhoods.Init(nullptr, NumCells);

	NeighborhoodChangedLastStep.Init(true, NumCells);
	NeighborhoodChangedThisStep.Init(false, NumCells);

	ParallelFor(NumCells, [&](int CellID)
	{
		// cell dimensions are twice as large as cluster dimensions
		int XCells = XClusters * XCellsPerCluster;
		int ZCells = ZClusters * ZCellsPerCluster;

		// derive grid coordinates from index
		int32 z = CellID / XCells;
		int32 x = CellID % XCells;

		// vertical coordinate above this cell
		int32 zUp = (z + 1) % ZCells;

		// vertical coordinate below this cell
		int32 zDown = (z - 1 + ZCells) % ZCells;

		// horizontal coordinate ahead of this cell
		int32 xUp = (x + 1) % XCells;

		// horizontal coordinate behind this cell
		int32 xDown = (x - 1 + XCells) % XCells;


		//uint32 NeighborhoodStart = 8 * CellID;

		TArray<int> Neighborhood
		{
			//assign lower neighborhood row IDs
			xDown + (XCells * zDown),
			x + (XCells * zDown),
			xUp + (XCells * zDown),

			//assign middle neighborhood row IDs
			xDown + (XCells * z),
			xUp + (XCells * z),

			// assign upper neighborhood row IDs
			xDown + (XCells * zUp),
			x + (XCells * zUp),
			xUp + (XCells * zUp)
		};

		TSharedPtr<TArray<int>> NeighborhoodPtr = MakeShared<TArray<int>>(Neighborhood);

		Neighborhoods[CellID] = NeighborhoodPtr;

	});
}

void AAutomataDriver::InitializeCellNeighborsOf()
{
	NeighborsOf = Neighborhoods;
}

void AAutomataDriver::InitializeCellProcessors()
{
	int MaxClustersPerInstance = int(ceilf(float(NumClusters) / float(Divisions)));

	Processors.Reserve(Divisions);
	for (int i = 0; i < Divisions; ++i)
	{
		TArray<int> ProcessorCells;
		ProcessorCells.Reserve(MaxClustersPerInstance * CellsPerCluster);

		int FirstCluster = i * MaxClustersPerInstance;
		int End = std::min(FirstCluster + MaxClustersPerInstance, NumClusters);

		for (int j = FirstCluster; j < End; ++j)
		{
			ProcessorCells.Append(CellsIDsFromCluster(j));
		}

		FAsyncTask<CellProcessor>* NewProcessor = new FAsyncTask<CellProcessor>(this, ProcessorCells);
		Processors.Add(NewProcessor);

	}
}

TArray<int> AAutomataDriver::CellsIDsFromCluster(const int ClusterID) const
{
	// Derive grid coordinates from cluster index
	int ClusterX = ClusterID % (int)XClusters;
	int ClusterZ = ClusterID / (int)XClusters;

	TArray<int> CellIDs;
	CellIDs.Reserve(CellsPerCluster);

	for (int z = 0; z < ZCellsPerCluster; ++z)
	{
		for (int x = 0; x < XCellsPerCluster; ++x)
		{
			CellIDs.Add((ClusterZ * ZCellsPerCluster + z) * (int)XClusters * XCellsPerCluster + XCellsPerCluster * ClusterX + x);
		}
	}

	return CellIDs;
}

void AAutomataDriver::SetCellNextCustomData(const int CellID)
{

	// register change based on state
	if (NextStates[CellID])
	{  // switch-off time is in the future, i.e. cell is still on
		*(CurrentDataSlots[CellID]) = TNumericLimits<float>::Max();
	}
	else // is off at next time
	{
		if (CurrentStates[CellID])  // was previously on
		{ // register switch-off time as being upcoming step
			*(CurrentDataSlots[CellID]) = NextStepTime;
		}
	}

}

void AAutomataDriver::SetCellNextCustomData(const TArray<int>& CellIDs)
{
	ParallelFor(CellIDs.Num(), [&](int32 i)
	{
		int CellID = CellIDs[i];

		if (NeighborhoodChangedLastStep[CellID] || ChangedLastStep[CellID])// register change based on state
		{
			//SetCellNextCustomData(CellID);
			// register change based on state
			if (NextStates[CellID])
			{  // switch-off time is in the future, i.e. cell is still on
				*(CurrentDataSlots[CellID]) = TNumericLimits<float>::Max();
			}
			else // is off at next time
			{
				if (CurrentStates[CellID])  // was previously on
				{ // register switch-off time as being upcoming step
					*(CurrentDataSlots[CellID]) = NextStepTime;
				}
			}
		}


	}/*, EParallelForFlags::BackgroundPriority*/);
}



void AAutomataDriver::ApplyCellRules(int CellID)
{
	int AliveNeighbors = GetCellAliveNeighbors(CellID);

	if (CurrentStates[CellID])
	{ // Any live cell with appropriate amount of neighbors survives
		NextStates[CellID] = SurviveRules[AliveNeighbors];
	}
	else
	{ // Any dead cell with appropriate amount of neighbors becomes alive
		NextStates[CellID] = BirthRules[AliveNeighbors];
	}

	//there has been a change of state
	if (NextStates[CellID] != CurrentStates[CellID])
	{
		ChangedThisStep[CellID] = true;
		for (int InfluencedCellID : *(NeighborsOf[CellID]))
		{
			NeighborhoodChangedThisStep[InfluencedCellID] = true;
		}
	}

}

void AAutomataDriver::ApplyCellRules(const TArray<int>& CellIDs)
{
	ParallelFor(CellIDs.Num(), [&](int32 i)
	{
		const int CellID = CellIDs[i];

		if (NeighborhoodChangedLastStep[CellID] || ChangedLastStep[CellID])
		{
			int AliveNeighbors = GetCellAliveNeighbors(CellID);

			if (CurrentStates[CellID])
			{ // Any live cell with appropriate amount of neighbors survives
				NextStates[CellID] = SurviveRules[AliveNeighbors];
			}
			else
			{ // Any dead cell with appropriate amount of neighbors becomes alive
				NextStates[CellID] = BirthRules[AliveNeighbors];
			}

			//there has been a change of state
			if (NextStates[CellID] != CurrentStates[CellID])
			{
				ChangedThisStep[CellID] = true;
				for (int InfluencedCellID : *(NeighborsOf[CellID]))
				{
					NeighborhoodChangedThisStep[InfluencedCellID] = true;
				}
			}

		}

	});
}



int AAutomataDriver::GetCellAliveNeighbors(const int CellID)
{
	//Query the cell's neighborhood to sum its alive neighbors
	int AliveNeighbors = 0;
	TArray<int> Neighborhood = *(Neighborhoods[CellID]);
	for (int NeighborID : Neighborhood)
	{
		AliveNeighbors = AliveNeighbors + CurrentStates[NeighborID];
	}
	return AliveNeighbors;
}

void AAutomataDriver::CellProcessorWork(const TArray<int>& CellIDs)
{
	ApplyCellRules(CellIDs);

	SetCellNextCustomData(CellIDs);

}

void AAutomataDriver::StepComplete()
{

	// have all the cells' next state calculated before sending to material
	// strictly speaking we only need to check the last one, but
	// checking all for safety
	for (FAsyncTask<CellProcessor>* Process : Processors)
	{
		Process->EnsureCompletion(false);
	}

	for (int i = 0; i < ClusterInstances.Num(); ++i)
	{
		UpdateInstance(i);
	}
	

	TimestepPropertyShift();

	// kick off calculation of next stage
	CurrentProcess = 0;
	MaterialToUpdate = 0;
	Processors[CurrentProcess]->StartBackgroundTask();

}

void AAutomataDriver::TimestepPropertyShift()
{
	NextStepTime = GetWorld()->GetTimeSeconds() + StepPeriod;

	ParallelFor(NumCells, [&](int32 CellID)
	{
		CurrentStates[CellID] = NextStates[CellID];

		//*(CurrentDataSlots[CellID]) = *(NextDataSlots[CellID]);

		NeighborhoodChangedLastStep[CellID] = NeighborhoodChangedThisStep[CellID];
		NeighborhoodChangedThisStep[CellID] = false;

		ChangedLastStep[CellID] = ChangedThisStep[CellID];
		ChangedThisStep[CellID] = false;

	}/*, EParallelForFlags::BackgroundPriority*/);
}

void AAutomataDriver::ProcessCompleted()
{
	// increment Process count, looping back to 0 when overflowing
	CurrentProcess = (CurrentProcess + 1) % Divisions;

	// kick off next process if required
	if (CurrentProcess != 0)
	{
		Processors[CurrentProcess]->EnsureCompletion();
		Processors[CurrentProcess]->StartBackgroundTask();
	}
}

void AAutomataDriver::TimerFired()
{
	StepComplete();
}

void AAutomataDriver::UpdateInstance(int Index)
{
	// Safety check
	//Processors[Index]->EnsureCompletion();

	// Apply changes to instance

	ClusterInstances[Index]->MarkRenderStateDirty();
	ClusterInstances[Index]->InstanceUpdateCmdBuffer.NumEdits++;

}



CellProcessor::CellProcessor(AAutomataDriver* Driver, TArray<int> CellIDs)
{
	this->Driver = Driver;
	this->CellIDs = CellIDs;
	
}

// Calculate state transitions for the cells this processor is responsible for
	void CellProcessor::DoWork()
	{
		//Driver->ProcessorWork(ClusterInstance, StartingIndex);
		Driver->CellProcessorWork(CellIDs);

		// Signal to Automata Driver that this processing has been completed
		Driver->ProcessCompleted();
	}



