#include "SVONPathFinder.h"
#include "SVONLink.h"
#include "Runtime/NavigationSystem/Public/NavigationData.h"

int SVONPathFinder::FindPath(const SVONLink& aStart, const SVONLink& aGoal, const FVector& aStartPos, const FVector& aTargetPos, FNavPathSharedPtr* oPath)
{
	myOpenSet.Empty();
	myClosedSet.Empty();
	myCameFrom.Empty();
	myFScore.Empty();
	myGScore.Empty();
	myCurrent = SVONLink();
	myGoal = aGoal;


	myOpenSet.Add(aStart);
	myCameFrom.Add(aStart, aStart);
	myGScore.Add(aStart, 0);
	myFScore.Add(aStart, HeuristicScore(aStart, myGoal)); // Distance to target

	int numIterations = 0;

	while (myOpenSet.Num() > 0)
	{
		
		float lowestScore = FLT_MAX;
		for (SVONLink& link : myOpenSet)
		{
			if (!myFScore.Contains(link) || myFScore[link] < lowestScore)
			{
				lowestScore = myFScore[link];
				myCurrent = link;
			}
		}

		myOpenSet.Remove(myCurrent);
		myClosedSet.Add(myCurrent);

		if (myCurrent == myGoal)
		{
			BuildPath(myCameFrom, myCurrent, aStartPos, aTargetPos, oPath);
			UE_LOG(UESVON, Display, TEXT("Pathfinding complete, iterations : %i"), numIterations);
			return 1;
		}

		const SVONNode& currentNode = myVolume.GetNode(myCurrent);

		TArray<SVONLink> neighbours;

		if (myCurrent.GetLayerIndex() == 0 && currentNode.myFirstChild.IsValid())
		{
			
			myVolume.GetLeafNeighbours(myCurrent, neighbours);
		}
		else
		{
			myVolume.GetNeighbours(myCurrent, neighbours);
		}

		for (const SVONLink& neighbour : neighbours)
		{
			ProcessLink(neighbour);
		}

		numIterations++;
	}

	UE_LOG(UESVON, Display, TEXT("Pathfinding failed, iterations : %i"), numIterations);
	return 0;
}

float SVONPathFinder::HeuristicScore( const SVONLink& aStart, const SVONLink& aTarget)
{
	/* Just using manhattan distance for now */
	float score = 0.f;

	FVector startPos, endPos;
	myVolume.GetLinkPosition(aStart, startPos);
	myVolume.GetLinkPosition(aTarget, endPos);
	switch (mySettings.myPathCostType)
	{
		case ESVONPathCostType::MANHATTAN:
			score = FMath::Abs(endPos.X - startPos.X) + FMath::Abs(endPos.Y - startPos.Y) + FMath::Abs(endPos.Z - startPos.Z);
			break;
		case ESVONPathCostType::EUCLIDEAN:
		default:
			score = (startPos - endPos).Size();
			break;
	}
	
	score *= (1.0f - (static_cast<float>(aTarget.GetLayerIndex()) / static_cast<float>(myVolume.GetMyNumLayers())) * mySettings.myNodeSizeCompensation);

	return score;
}

float SVONPathFinder::GetCost( const SVONLink& aStart, const SVONLink& aTarget)
{
	float cost = 0.f;

	// Unit cost implementation
	if (mySettings.myUseUnitCost)
	{
		cost = mySettings.myUnitCost;
	}
	else
	{


		FVector startPos(0.f), endPos(0.f);
		const SVONNode& startNode = myVolume.GetNode(aStart);
		const SVONNode& endNode = myVolume.GetNode(aTarget);
		myVolume.GetLinkPosition(aStart, startPos);
		myVolume.GetLinkPosition(aTarget, endPos);
		cost = (startPos - endPos).Size();
	}

	cost *= (1.0f - (static_cast<float>(aTarget.GetLayerIndex()) / static_cast<float>(myVolume.GetMyNumLayers())) * mySettings.myNodeSizeCompensation);
		
	return cost;
}

void SVONPathFinder::ProcessLink(const SVONLink& aNeighbour)
{
	if (aNeighbour.IsValid())
	{
		if (myClosedSet.Contains(aNeighbour))
			return;

		if (!myOpenSet.Contains(aNeighbour))
		{
			myOpenSet.Add(aNeighbour);

			if (mySettings.myDebugOpenNodes)
			{
				FVector pos;
				myVolume.GetLinkPosition(aNeighbour, pos);
				mySettings.myDebugPoints.Add(pos);
			}

		}

		float t_gScore = FLT_MAX;
		if (myGScore.Contains(myCurrent))
			t_gScore = myGScore[myCurrent] + GetCost(myCurrent, aNeighbour);
		else
			myGScore.Add(myCurrent, FLT_MAX);

		if (t_gScore >= (myGScore.Contains(aNeighbour) ? myGScore[aNeighbour] : FLT_MAX))
			return;

		myCameFrom.Add(aNeighbour, myCurrent);
		myGScore.Add(aNeighbour, t_gScore);
		myFScore.Add(aNeighbour, myGScore[aNeighbour] + (mySettings.myEstimateWeight * HeuristicScore(aNeighbour, myGoal)));
	}
}

void SVONPathFinder::BuildPath(TMap<SVONLink, SVONLink>& aCameFrom, SVONLink aCurrent, const FVector& aStartPos, const FVector& aTargetPos, FNavPathSharedPtr* oPath)
{
	
	FVector pos;

	TArray<FVector> points;
	float CosValue = 0.f;
 	float NextCosValue = 0.f;

	if (!oPath || !oPath->IsValid())
		return;

	while (aCameFrom.Contains(aCurrent) && !(aCurrent == aCameFrom[aCurrent]))
	{
		aCurrent = aCameFrom[aCurrent];
		myVolume.GetLinkPosition(aCurrent, pos);
		points.Add(pos);
		
	}

	if (points.Num() > 1)
	{
		points[0] = aTargetPos;
		points[points.Num() - 1] = aStartPos;
	}

	Smooth_Chaikin(points, mySettings.mySmoothingIterations);

	for (int i = points.Num() - 1; i >= 0; i--)
	{
		//Add Cosine Similarity
		CosValue = (points[i].X*aTargetPos.X + points[i].Y*aTargetPos.Y) / (FMath::Sqrt(points[i].X*points[i].X + aTargetPos.X*aTargetPos.X)*FMath::Sqrt(points[i].Y*points[i].Y + aTargetPos.Y*aTargetPos.Y));

		if (i != 0)
		{
			NextCosValue = (points[i - 1].X*aTargetPos.X + points[i - 1].Y*aTargetPos.Y) / (FMath::Sqrt(points[i - 1].X*points[i - 1].X + aTargetPos.X*aTargetPos.X)*FMath::Sqrt(points[i - 1].Y*points[i - 1].Y + aTargetPos.Y*aTargetPos.Y));
		}

		if (CosValue > -0.5 && CosValue < 0.5 && CosValue != 0 && NextCosValue > -0.5 && NextCosValue < 0.5 && NextCosValue != 0)
		{
			i--;
			continue;
		}
		oPath->Get()->GetPathPoints().Add(points[i]);
	}
	
	
}

void SVONPathFinder::Smooth_Chaikin(TArray<FVector>& somePoints, int aNumIterations)
{
	for (int i = 0; i < aNumIterations; i++)
	{
		for (int j = 0; j < somePoints.Num() - 1; j += 2)
		{
			FVector start = somePoints[j];
			FVector end = somePoints[j + 1];
			if (j > 0)
				somePoints[j] = FMath::Lerp(start, end, 0.25f);
			FVector secondVal = FMath::Lerp(start, end, 0.75f);
			somePoints.Insert(secondVal, j + 1);
		}
		somePoints.RemoveAt(somePoints.Num() - 1);
	}
}
