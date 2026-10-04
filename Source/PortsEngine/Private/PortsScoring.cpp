// Legacy score = Wealth (florins, posts, land) + Family + Reputation + Balance bonus (lowest of the three).
#include "PortsRules.h"

// The fractional sums must round at every step exactly as JavaScript does:
// do not let the compiler merge a multiply and an add into one operation.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

namespace Ports
{
	FPortsScore ScorePlayer(const FPortsPlayer& P)
	{
		const FPortsData& D = FPortsData::Get();
		FPortsScore S;
		// Rounded down as JavaScript's Math.floor does (a house can start with a debt).
		S.wealth = FMath::FloorToInt32(static_cast<double>(P.florins) / D.Int(TEXT("scoring.florinsPerPoint"))) + P.posts.Num() * D.Int(TEXT("scoring.pointsPerPost")) + P.land.Num() * D.Int(TEXT("scoring.pointsPerLand"));
		S.family = FamilyTotal(P) * D.Int(TEXT("scoring.pointsPerFamily"));
		// Reputation: full points up to the soft cap, then 1 point per `reputationHighRate` above it.
		const int32 Soft = D.Int(TEXT("scoring.reputationSoftCap"));
		S.reputation = FMath::Min(P.reputation, Soft) * D.Int(TEXT("scoring.reputationPoints")) + FMath::Max(0, P.reputation - Soft) / D.Int(TEXT("scoring.reputationHighRate"));
		S.balance = FMath::Min3(S.wealth, S.family, S.reputation);
		S.total = S.wealth + S.family + S.reputation + S.balance;
		return S;
	}

	double ScoreTotal(double Florins, int32 Posts, int32 Land, double FamilyMembers, double Reputation)
	{
		const FPortsData& D = FPortsData::Get();
		const double Wealth = FMath::FloorToDouble(Florins / D.Number(TEXT("scoring.florinsPerPoint"))) + Posts * D.Number(TEXT("scoring.pointsPerPost")) + Land * D.Number(TEXT("scoring.pointsPerLand"));
		const double Family = FamilyMembers * D.Number(TEXT("scoring.pointsPerFamily"));
		const double Soft = D.Number(TEXT("scoring.reputationSoftCap"));
		const double Rep = FMath::Min(Reputation, Soft) * D.Number(TEXT("scoring.reputationPoints")) + FMath::FloorToDouble(FMath::Max(0.0, Reputation - Soft) / D.Number(TEXT("scoring.reputationHighRate")));
		const double Balance = FMath::Min3(Wealth, Family, Rep);
		return Wealth + Family + Rep + Balance;
	}

	// Final ranking. Ties: higher reputation, then more family; otherwise shared.
	TArray<FPortsRank> RankPlayers(const FPortsState& State)
	{
		TArray<FPortsRank> Rows;
		for (const FPortsPlayer& P : State.players)
		{
			const FPortsScore S = ScorePlayer(P);
			FPortsRank R;
			R.id = P.id;
			R.name = P.name;
			R.wealth = S.wealth; R.family = S.family; R.reputation = S.reputation; R.balance = S.balance; R.total = S.total;
			R.familyCount = FamilyTotal(P);
			R.rep = P.reputation;
			Rows.Add(R);
		}
		Rows.StableSort([](const FPortsRank& A, const FPortsRank& B)
		{
			if (A.total != B.total) return A.total > B.total;
			if (A.rep != B.rep) return A.rep > B.rep;
			return A.familyCount > B.familyCount;
		});
		int32 Place = 0;
		for (int32 i = 0; i < Rows.Num(); i++)
		{
			if (i == 0 || Rows[i - 1].total != Rows[i].total || Rows[i - 1].rep != Rows[i].rep || Rows[i - 1].familyCount != Rows[i].familyCount) Place = i + 1;
			Rows[i].place = Place;
		}
		return Rows;
	}

	// The single house in last place right now (lowest score; ties go to the
	// house with fewer florins, then the later seat).
	int32 LastPlaceId(const FPortsState& State)
	{
		bool bHave = false;
		int32 WorstId = 0, WorstTotal = 0, WorstFlorins = 0;
		for (const FPortsPlayer& P : State.players)
		{
			const int32 T = ScorePlayer(P).total;
			if (!bHave || T < WorstTotal || (T == WorstTotal && P.florins <= WorstFlorins))
			{
				bHave = true;
				WorstId = P.id; WorstTotal = T; WorstFlorins = P.florins;
			}
		}
		return WorstId;
	}
}
