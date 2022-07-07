// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

//class AAutomataDriver;

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "AutomataDriver.generated.h"


// CellProcessor is used to asynchronously update the cell states that it is responsible for,
// so that these calculations don't cause a performance bottleneck by being carried out all at once

// Asynchtask management in UE4 is poorly documented. There are seemingly many different kinds of manager types, and their benefits/tradeoffs are not clear to me.
// Important elements of getting it to work are
// 1. that the class inherits from FNonAbandonableTask,
// 2. That it be a friend class of the FAsyncTask (other kinds of async classes may be possible)
// 3. the GetStatId() function definition boilerplate needs to be in place

class CellProcessor : public FNonAbandonableTask
{
	friend class FAsyncTask<CellProcessor>;

public:

	// Initialization largely consists of getting pointers from the Driver
	CellProcessor(AAutomataDriver* Driver, TArray<int> CellIDs);


protected:

	// AutomataDriver this processor is working for
	class AAutomataDriver* Driver = nullptr;

	UPROPERTY()
		TArray<int> CellIDs;


public:
	
	// FAsyncTask boilerplate function, defining the task it works on when called
	// In this case: calculating updated cell states
	void DoWork();

	//FAsyncTask boilerplate. Do not remove
	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(ExampleAsyncTask, STATGROUP_ThreadPoolAsyncTasks);
	}
};


UCLASS()
class AAutomataDriver : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AAutomataDriver();

protected:

	virtual void PreInitializeComponents() override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;

	virtual void InitializeMaterial();

	virtual void InitializeInstances();

	virtual void InitializeTransforms();

	virtual void RunProcessesOnce();

	virtual TArray<int> CellsIDsFromCluster(const int ClusterID) const;

	virtual void InitializeCellRules();

	virtual void InitializeCellStates();

	virtual void InitializeCellCustomData();

	virtual void InitializeCellNeighborhoods();

	virtual void InitializeCellNeighborsOf();

	virtual void InitializeCellProcessors();

	virtual void StartingDataSetup();



	virtual void SetCellNextCustomData(int CellID);
	virtual void SetCellNextCustomData(const TArray<int>& CellIDs);

	virtual void ApplyCellRules(int CellID);
	virtual void ApplyCellRules(const TArray<int>& CellIDs);

	virtual void TimestepPropertyShift();

	virtual int GetCellAliveNeighbors(int CellID);

	//Set that stores the birth rules for the automata
	TArray<bool> BirthRules;
	//Set that stores the survival rules for the automata
	TArray<bool> SurviveRules;

	TArray<bool> CurrentStates;
	TArray<bool> NextStates;

	TArray<bool> NeighborhoodChangedThisStep;
	TArray<bool> NeighborhoodChangedLastStep;

	TArray<bool> ChangedThisStep;
	TArray<bool> ChangedLastStep;

	TArray<float*> CurrentDataSlots;
	TArray<float*>  NextDataSlots;
	TArray<float*> SwitchTimeSlots;



	TArray<TSharedPtr<TArray<int>>> Neighborhoods;
	TArray<TSharedPtr<TArray<int>>> NeighborsOf;

	

	// Array of CellProcessor objects that are responsible for incrementing an associated instance collection
	TArray<FAsyncTask<CellProcessor>*> Processors;

	// Array that stores the instance collections
	UPROPERTY()
		TArray<UInstancedStaticMeshComponent*> ClusterInstances;
	

	// Mesh that will be instanced to form the grid- typically a simple square
	UPROPERTY(Blueprintable, EditAnywhere)
		UStaticMesh* Mesh;

	// Material that will be instanced and applied to the mesh.
	// This needs to be specifically made for automata in order for it to display anything interesting
	UPROPERTY(Blueprintable, EditAnywhere)
		UMaterialInterface* Mat;

	// Dynamic material (will use Mat as its basis)
	UMaterialInstanceDynamic* DynMaterial;

	// Number of discrete mesh instance collections
	// Each collection will have its materials updated at a different time, smoothing framerate in many scenarios
	UPROPERTY(Blueprintable, EditAnywhere)
		int Divisions = 2;

	UPROPERTY(Blueprintable, EditAnywhere)
		int XCellsPerCluster = 2;
	UPROPERTY(Blueprintable, EditAnywhere)
		int ZCellsPerCluster = 2;

	int NumClusters;
	int CellsPerCluster;
	int NumCells;
	int NumCustomData;

	// Probability when initializing that a cell will start off alive.
	// Functionally ranges from 0 to 1.
	UPROPERTY(Blueprintable, EditAnywhere)
		float Probability = 0.4; 

	// User-set string that defines the birth rules for the automata
	// Capable of accepting non-digit characters, but they will be ignored
	UPROPERTY(Blueprintable, EditAnywhere)
		FString BirthString = TEXT("3");

	// User-set string that defines the survival rules for the automata
	// Capable of accepting non-digit characters, but they will be ignored
	UPROPERTY(Blueprintable, EditAnywhere)
		FString SurviveString = TEXT("23");





	//Horizontal dimension of the grid of clusters
	//Since each cluster is a square of 2x2 cells, the horizontal dimension of the grid of cells is 2-times this amount.
	UPROPERTY(Blueprintable, EditAnywhere)
		int XClusters = 300;

	//Vertical dimension of the grid of clusters
	//Since each cluster is a square of 2x2 cells, the horizontal dimension of the grid of cells is 2-times this amount.
	UPROPERTY(Blueprintable, EditAnywhere)
		int ZClusters = 300;

	// Spacing that determines how far adjacent clusters should be placed away from each other
	// The square mesh used has a 200x200 unit area.
	UPROPERTY(Blueprintable, EditAnywhere)
		int Offset = 1;

	// time per automata step in seconds
	UPROPERTY(Blueprintable, EditAnywhere) 
		float StepPeriod = 0.01;

	// Exponent that drives how quickly a switched-off cell fades into the off state
	// An exponent of 1 will fade linearly over the transition period. A higher exponent will fade out quicker initially, and a lower exponent will fade out slower initially.
	UPROPERTY(Blueprintable, EditAnywhere) 
		float PhaseExponent = 201;

	// Simple float used to store the time of the next step transition
	float NextStepTime = 0;

	// "On" state cell color
	UPROPERTY(Blueprintable, EditAnywhere)
		FLinearColor OnColor = FLinearColor(0.6, 0, 0.6, 1);

	// "Off" state cell color
	UPROPERTY(Blueprintable, EditAnywhere)
		FLinearColor OffColor = FLinearColor(0.0, 0, 0.0, 1);

	// Material property used to control emissive value
	UPROPERTY(Blueprintable, EditAnywhere) 
		float EmissiveMultiplier = 20;

	// how many automata steps a dead cell takes to fade out after death
	UPROPERTY(Blueprintable, EditAnywhere) 
		float StepsToFade = 1000;

	// Timer that fires once for each instance collection, and one additional time to signal the end of an automata step
	FTimerHandle StepTimer;
	FTimerHandle InstanceUpdateTimer;

	// Handles automata step completion and transition into next step
	UFUNCTION()
		void StepComplete();

	// send updates to instance collection denoted by Index
	UFUNCTION()
		void UpdateInstance(int Index);

	// Called when StepTimer is fired
	UFUNCTION()
		void TimerFired();

	// Keeps track of CellProcessor to be fired off
	int CurrentProcess = 0;
	// Keeps track of which material instance to update
	int MaterialToUpdate = 0;


public:	

	// called by a Processor when it has finished its asynchronous task
	void ProcessCompleted();

	virtual void CellProcessorWork(const TArray<int>& CellIDs);

};


