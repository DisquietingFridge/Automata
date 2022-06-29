// Fill out your copyright notice in the Description page of Project Settings.


#include "AutomataDriver.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Misc/Char.h"
#include "Async/Async.h"
#include "Async/AsyncWork.h"
#include "Containers/ArrayView.h"


const uint8 NumCustomData = 9;

UAutomataCell::UAutomataCell()
{

}

void UAutomataCell::ApplyCellRules()
{
	int AliveNeighbors = GetCellAliveNeighbors();

	if (CurrentState)
	{ // Any live cell with appropriate amount of neighbors survives
		NextState = SurviveRules.Contains(AliveNeighbors);
	}
	else
	{ // Any dead cell with appropriate amount of neighbors becomes alive
		NextState = BirthRules.Contains(AliveNeighbors);
	}
}

int UAutomataCell::GetCellAliveNeighbors()
{
	 //Query the cell's neighborhood to sum its alive neighbors
	uint8 AliveNeighbors = 0;
	for (UAutomataCell* Neighbor : Neighborhood)
	{
		AliveNeighbors = AliveNeighbors + Neighbor->CurrentState;
	}
	return AliveNeighbors;
}

// Sets default values
AAutomataDriver::AAutomataDriver()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateOptionalDefaultSubobject<USceneComponent>(TEXT("Root Component"));
	
}

void AAutomataDriver::PreInitializeComponents()
{
	Super::PreInitializeComponents();

	// Create material and set up properties
	InitializeMaterial();

	//Set up InstanceComponents
	InitializeInstances();
	
}

void AAutomataDriver::PostInitializeComponents()
{


	Super::PostInitializeComponents();

	InitializeCellArray();

	InitializeCellRules();


	InitializeCellStates();

	InitializeTransforms();

	InitializeCellCustomData();

	InitializeCellNeighborhoods();


	InitializeCellProcessors();

	RunProcessesOnce();
}


// Called when the game starts or when spawned
void AAutomataDriver::BeginPlay()
{
	Super::BeginPlay();
		
	// we are ready to start the iteration steps.
	GetWorldTimerManager().SetTimer(StepTimer, this, &AAutomataDriver::TimerFired, StepPeriod/(Divisions + 1), true);
}

void AAutomataDriver::InitializeMaterial()
{

	// Create material and set up properties
	DynMaterial = UMaterialInstanceDynamic::Create(Mat, this);

	DynMaterial->SetScalarParameterValue("PhaseExponent", PhaseExponent);
	DynMaterial->SetScalarParameterValue("EmissiveMultiplier", EmissiveMultiplier);
	DynMaterial->SetVectorParameterValue("OnColor", OnColor);
	DynMaterial->SetVectorParameterValue("OffColor", OffColor);
	DynMaterial->SetScalarParameterValue("FadePerSecond", 1 / (StepPeriod * StepsToFade));
}

void AAutomataDriver::InitializeInstances()
{
	ClusterInstances.Reserve(Divisions);
	for (uint32 i = 0; i < Divisions; ++i)
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
	uint32 NumClusters = GetNumClusters();

	TArray<FTransform> Transforms;
	Transforms.Init(FTransform(), NumClusters);

	// calculate transforms for each cluster
	ParallelFor(NumClusters, [&](int32 ClusterID)
	{
		// derive grid coordinates from cluster index
		int32 ClusterX = ClusterID % XDim;
		int32 ClusterZ = ClusterID / XDim;

		//Instance's transform is based on its grid coordinate
		Transforms[ClusterID] = FTransform((FVector(ClusterX, 0, ClusterZ) * Offset));
	});

	// the last ClusterInstance component may have fewer cells assigned to it
	int MaxClustersPerInstance = int(ceilf(float(NumClusters) / float(Divisions)));

	// Add instances to each ClusterInstance component, applying appropriate transform
	for (uint32 ClusterID = 0; ClusterID < NumClusters; ++ClusterID)
	{
		int InstanceIndex = ClusterID / MaxClustersPerInstance;
		ClusterInstances[InstanceIndex]->AddInstance(Transforms[ClusterID]);
	}

}

void AAutomataDriver::RunProcessesOnce()
{
	NextStepTime = GetWorld()->GetTimeSeconds() + StepPeriod;

	// start the processes (to calculate Next Step for all the cells)
	CurrentProcess = 0;
	Processors[CurrentProcess]->StartSynchronousTask();
	// having kicked off the first process, they should all cascade to completion, until the final one is complete
	// wait until the last process in this cascade is complete
	Processors.Last()->EnsureCompletion(false);
}


TArray<uint32> AAutomataDriver::CellsIDsFromCluster(uint32 ClusterID)
{
	// Derive grid coordinates from cluster index
	int32 ClusterX = ClusterID % XDim;
	int32 ClusterZ = ClusterID / XDim;

	//Define CellIDs contained within this cluster
	TArray<uint32> CellIDs
	{
		(2 * ClusterZ * 2 * XDim) + (2 * ClusterX), // bottom left cell ID
		(2 * ClusterZ * 2 * XDim) + (2 * ClusterX + 1), // bottom right cell ID
		((2 * ClusterZ + 1) * 2 * XDim) + (2 * ClusterX), // upper left cell ID
		((2 * ClusterZ + 1) * 2 * XDim) + (2 * ClusterX + 1), // upper right cell ID
	};

	return CellIDs;
}


void AAutomataDriver::InitializeCellArray()
{
	CellArray.Reserve(GetNumCells());
	
	for (uint32 i = 0; i < GetNumCells(); ++i)
	{
		UAutomataCell* NewCell = NewObject<UAutomataCell>(this);
		CellArray.Add(NewCell);
		CellArray[i]->ID = i;
	}
}

void AAutomataDriver::InitializeCellRules()
{
	for (TCHAR character : BirthString)
	{
		if (TChar<TCHAR>::IsDigit(character))
		{
			BirthRules.Add(TChar<TCHAR>::ConvertCharDigitToInt(character));
		}
	}

	for (TCHAR character : SurviveString)
	{
		if (TChar<TCHAR>::IsDigit(character))
		{
			SurviveRules.Add(TChar<TCHAR>::ConvertCharDigitToInt(character));
		}
	}

	for (UAutomataCell* Cell : CellArray)
	{
		Cell->BirthRules = this->BirthRules;
		Cell->SurviveRules = this->SurviveRules;
	}
}

void AAutomataDriver::InitializeCellStates()
{
	for (UAutomataCell* Cell : CellArray)
	{
		Cell->CurrentState = (float(rand()) < (float(RAND_MAX) * Probability));
		Cell->NextState = 0;
	}
}

void AAutomataDriver::InitializeCellCustomData()
{
	int MaxClustersPerInstance = int(ceilf(float(GetNumClusters()) / float(Divisions)));


	ParallelFor(GetNumClusters(), [&](int32 ClusterID)
	{
		// Determine which instance collection cluster belongs to
		int InstanceIndex = ClusterID / MaxClustersPerInstance;

		// define cluster ID local to the cluster's instance collection
		int InstanceClusterID = ClusterID - InstanceIndex * MaxClustersPerInstance;

		int ClusterTimeIndex = InstanceClusterID * 9 + 8;

		TArray<uint32> CellIDs = CellsIDsFromCluster(ClusterID);

		// for each cell within the cluster
		for (uint8 Quadrant = 0; Quadrant < 4; ++Quadrant)
		{

			UAutomataCell* Cell = CellArray[CellIDs[Quadrant]];

			uint32 CurrentDataIndex = NumCustomData * InstanceClusterID + (2 * Quadrant);
			uint32 NextDataIndex = CurrentDataIndex + 1;

			Cell->NextDataSlot = &(ClusterInstances[InstanceIndex]->PerInstanceSMCustomData[NextDataIndex]);
			Cell->CurrentDataSlot = &(ClusterInstances[InstanceIndex]->PerInstanceSMCustomData[CurrentDataIndex]);
			Cell->SwitchTimeSlot = &(ClusterInstances[InstanceIndex]->PerInstanceSMCustomData[ClusterTimeIndex]);

			*(Cell->CurrentDataSlot) = -2 * (StepPeriod * StepsToFade);

			// Set "Current" data index to show early switchoff time.
			//ClusterInstances[InstanceIndex]->PerInstanceSMCustomData[NextDataIndex - 1] = -2 * (StepPeriod * StepsToFade);

			SetCellNextCustomData(Cell);

		}

	});

}

void AAutomataDriver::SetCellNextCustomData(UAutomataCell* Cell)
{

	// register change based on state
	if (Cell->NextState)
	{  // switch-off time is in the future, i.e. cell is still on
		*(Cell->NextDataSlot) = NextStepTime + 3 * StepPeriod;
	}
	else // is off at next time
	{
		if (Cell->CurrentState)  // was previously on
		{ // register switch-off time as being upcoming step
			*(Cell->NextDataSlot) = NextStepTime;
		}
		else // preserve old switch-off time
		{
			*(Cell->NextDataSlot) = *(Cell->CurrentDataSlot);
		}
	}

}

void AAutomataDriver::InitializeCellNeighborhoods()
{

	ParallelFor(GetNumCells(), [&](int32 CellID)
	{
		// cell dimensions are twice as large as cluster dimensions
		int XCells = XDim * 2;
		int ZCells = ZDim * 2;

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

		TArray<UAutomataCell*> Neighborhood
		{
			//assign lower neighborhood row IDs
			CellArray[xDown + (XCells * zDown)],
			CellArray[x + (XCells * zDown)],
			CellArray[xUp + (XCells * zDown)],

			//assign middle neighborhood row IDs
			CellArray[xDown + (XCells * z)],
			CellArray[xUp + (XCells * z)],

			// assign upper neighborhood row IDs
			CellArray[xDown + (XCells * zUp)],
			CellArray[x + (XCells * zUp)],
			CellArray[xUp + (XCells * zUp)]
		};

		CellArray[CellID]->Neighborhood = Neighborhood;
	});
}

void AAutomataDriver::InitializeCellProcessors()
{
	int MaxClustersPerInstance = int(ceilf(float(GetNumClusters()) / float(Divisions)));

	Processors.Reserve(Divisions);
	for (uint32 i = 0; i < Divisions; ++i)
	{
		TArray<UAutomataCell*> Cells;
		Cells.Reserve(MaxClustersPerInstance * 4);

		uint32 FirstCluster = i * MaxClustersPerInstance;
		uint32 End = std::min((i + 1) * MaxClustersPerInstance, GetNumClusters());

		for (uint32 j = FirstCluster; j < End; ++j)
		{
			TArray<uint32> CellIDs = CellsIDsFromCluster(j);
			for (uint32 ID : CellIDs)
			{
				Cells.Add(CellArray[ID]);
			}
		}	

		FAsyncTask<CellProcessor>* NewProcessor = new FAsyncTask<CellProcessor>(this, Cells);
		Processors.Add(NewProcessor);
		
	}
}

void AAutomataDriver::CellProcessorWork(const TArray<UAutomataCell*>& Cells)
{

	ParallelFor(Cells.Num(), [&](int32 i)
	{
		UAutomataCell* Cell = Cells[i];
		*(Cell->SwitchTimeSlot) = NextStepTime;

		Cell->ApplyCellRules();

		//shift material state in time, so "Next" state is assigned to "Current"
		*(Cell->CurrentDataSlot) = *(Cell->NextDataSlot);

		// register change based on state
		SetCellNextCustomData(Cell);
	},EParallelForFlags::BackgroundPriority);

}

void AAutomataDriver::TimerFired()
{
	// if there are still materials to update
	if (MaterialToUpdate < Divisions)
	{
		UpdateInstance(MaterialToUpdate);
		MaterialToUpdate++;	
	}
	else
	{
		MaterialToUpdate = 0;
		// no more materials to update- automata step is complete
		StepComplete();
	}
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
	
	NextStepTime = GetWorld()->GetTimeSeconds() + StepPeriod;

	// make new Current state the old Next state.

	ParallelFor(CellArray.Num(), [&](int32 i) 
	{
		CellArray[i]->CurrentState = CellArray[i]->NextState;
	}, EParallelForFlags::BackgroundPriority);
	
	// kick off calculation of next stage
	CurrentProcess = 0;
	MaterialToUpdate = 0;
	Processors[CurrentProcess]->StartBackgroundTask();

}

void AAutomataDriver::UpdateInstance(uint32 Index)
{
	// Safety check
	//Processors[Index]->EnsureCompletion();

	// Apply changes to instance
	
	ClusterInstances[Index]->MarkRenderStateDirty();
	ClusterInstances[Index]->InstanceUpdateCmdBuffer.NumEdits++;
	

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



CellProcessor::CellProcessor(AAutomataDriver* Driver, TArray<UAutomataCell*> Cells)
{
	this->Driver = Driver;
	this->Cells = Cells;
	
}



// Calculate state transitions for the cells this processor is responsible for
	void CellProcessor::DoWork()
	{
		//Driver->ProcessorWork(ClusterInstance, StartingIndex);
		Driver->CellProcessorWork(Cells);

		// Signal to Automata Driver that this processing has been completed
		Driver->ProcessCompleted();
	}



