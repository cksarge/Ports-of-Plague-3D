// The board: sea, land, lakes, rivers, routes and cities, built from the data
// files when the game starts.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PortsMapActor.generated.h"

class UProceduralMeshComponent;
class UStaticMesh;
class UMaterialInterface;

// A city's name on the map.
struct FPortsCityLabel
{
	FString CityId;
	FText Name;
	// Where the web map anchors the label, and which way the text runs from there:
	// -1 ends at the anchor, 0 is centred on it, 1 starts at it.
	FVector Anchor = FVector::ZeroVector;
	int32 Align = 1;
	// Where plague pips (above) and house banners (below) are drawn.
	FVector PipAnchor = FVector::ZeroVector;
	FVector TokenAnchor = FVector::ZeroVector;
	bool bHome = false;
};

// A house's trading post at a city: its colour and crest, and how many of its family live there.
struct FPortsCityToken
{
	FLinearColor Color = FLinearColor::White;
	FString Glyph;
	// The house's crest shape, which names its flag picture.
	FString Crest;
	int32 Family = 0;
	// This house has closed the city's gates.
	bool bGatesClosed = false;
	// Where its flag's pole stands on the board.
	FVector Foot = FVector::ZeroVector;
};

// What the game currently shows at a city.
struct FPortsCityView
{
	// 1 to 3 while the city is Stricken, otherwise 0.
	int32 Severity = 0;
	TArray<FPortsCityToken> Tokens;
};

// A short message rising from a city ("+9ƒ", "Infected!").
struct FPortsFloater
{
	FVector Position = FVector::ZeroVector;
	FString Text;
	bool bGain = true;
	double Start = 0;
};

// The value of a route, shown in a small disc at its middle.
struct FPortsRouteBadge
{
	FString RouteId;
	FVector Position = FVector::ZeroVector;
	int32 Value = 0;
	bool bSea = true;
};

// What the finale stands on the board at one moment (see PortsMapFinale.cpp).
struct FPortsFinaleProps
{
	double Time = 0;
	// Which way is right and up on the screen, so that sparks and flames can face the camera.
	FVector Right = FVector::RightVector, Up = FVector::UpVector;
	// Sparks drifting up over the whole board.
	int32 Embers = 0;
	// A family member standing by their house's home: Pop is how far they have appeared, Fall how far they have fallen.
	struct FFigure { FVector2D At; FLinearColor Color; float Pop; float Fall; };
	TArray<FFigure> Figures;
	// A candle by a house's home: lit for a house that took a stand.
	struct FCandle { FVector2D At; bool bLit; float Rise; };
	TArray<FCandle> Candles;
	// A house's Legacy as a tower in four storeys (wealth, family, reputation, balance), each as high as it has grown so far.
	struct FTower { FVector2D At; float Parts[4]; bool bWinner; };
	TArray<FTower> Towers;
	// The crown coming down over the winner's city (Drop from 0, high above, to 1, in place), and what falls round it.
	bool bCrown = false;
	FVector2D CrownAt = FVector2D::ZeroVector;
	float CrownDrop = 0;
	TArray<FLinearColor> Confetti;
};

UCLASS()
class PORTSOFPLAGUE_API APortsMapActor : public AActor
{
	GENERATED_BODY()

public:
	APortsMapActor();

	virtual void BeginPlay() override;

	const TArray<FPortsCityLabel>& GetCityLabels() const { return CityLabels; }
	const TArray<FPortsRouteBadge>& GetRouteBadges() const { return RouteBadges; }

	// A route's path in map pixels, from its city A to its city B.
	const TArray<FVector2D>* FindRouteCurve(const FString& RouteId) const { return RouteCurves.Find(RouteId); }

	// Where a city stands on the board.
	FVector CityLocation(const FString& CityId) const;

	virtual void Tick(float DeltaSeconds) override;

	// Shows the game as it stands: city colours by plague state, pips, banners and closed gates.
	// Selectable cities glow (a choice is open); highlighted routes are drawn in gold.
	void ApplyState(const struct FPortsState& State, const TArray<FString>& Selectable = TArray<FString>(), const TArray<FString>& HighlightRoutes = TArray<FString>());
	// With no game running: every city as at the start.
	void ClearState();
	const FPortsCityView* FindCityView(const FString& CityId) const { return CityViews.Find(CityId); }
	const TArray<FPortsFloater>& GetFloaters() const { return Floaters; }
	void FloatText(const FString& CityId, const FString& Text, bool bGain);

	// A house's ship (or cart) travels a route from one end; OnDone runs when it arrives.
	void AnimateShipment(const FString& RouteId, const FString& From, const FLinearColor& Color, bool bInfected, TFunction<void()> OnDone);
	bool IsAnimating() const { return bShipping; }
	// Where the house's ship or cart has got to on its voyage, in map pixels.
	FVector2D GetShipmentPixel() const { return ShipmentAt; }

	// The finale: draws its props for this moment, replays a voyage, and shows the plague's stain at chosen cities only.
	// How much of the board's lettering and markers shows: 0 everything; 1 no route values, pips, flags, rings or
	// passing traffic; 2 no city names either. The finale clears the board so its own show can be seen.
	void SetQuiet(int32 Level);
	int32 GetQuiet() const { return Quiet; }
	void DrawFinale(const FPortsFinaleProps& Props);
	void ClearFinale();
	void ReplayVoyage(const FString& RouteId, const FString& From, const FLinearColor& Color, TFunction<void()> OnDone);
	void ShowStainsAt(const TArray<FString>& CityIds);

	// The city at a point of the map (in map pixels), if one is within reach.
	FString CityAt(const FVector2D& Pixel, double Reach = 14.0) const;

private:
	void BuildSea();
	void BuildFrame();
	void BuildLand();
	void BuildForests();
	bool InTown(const FVector2D& Pixel) const;
	// How high something lying on the board sits at a point (map pixels): on the land, or on the sea.
	double GroundAt(const FVector2D& Pixel, double Lift) const;
	void BuildWater();
	void BuildRoutes();
	void BuildDecorations();
	void BuildCities();
	// Builds one city's model again in the colours of its state (0 as built, 1 Stricken, 2 Aftermath).
	void BuildTown(int32 CityIndex, int32 Look);
	struct FRing { int32 City; FLinearColor Color; double Width; bool bDashed; };
	void BuildRings(const TArray<FRing>& Rings);
	// The houses' flags and closed gates, from what CityViews holds.
	void BuildFlags();
	class UMaterialInstanceDynamic* AddBlock(UStaticMesh* Shape, const FVector2D& Pixel, double BaseHeight, const FVector& SizeInPixels, const FLinearColor& Color, class UStaticMeshComponent** OutBlock = nullptr);
	void BuildHighlights(const TArray<FString>& RouteIds);

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Surface;

	// The land and everything standing on it: these cast shadows, the flat board does not.
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Models;

	// How far every point is from the coast, as a picture for the water's material.
	UPROPERTY()
	TObjectPtr<class UTexture2D> CoastField;

	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> SeaPaint;

	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> LakePaint;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> SurfaceMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> ColorMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> ParchmentMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> TableMaterial;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> ConeMesh;

	// The cities' models, one mesh section each.
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Towns;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Flags;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FlagMaterial;

	// The state each city's model was last built for.
	TArray<int32> TownLooks;

	UPROPERTY()
	TArray<TObjectPtr<class UMaterialInstanceDynamic>> Paints;

	// A ship or cart on its way along a route.
	struct FVessel
	{
		UProceduralMeshComponent* Model = nullptr;
		TArray<FVector2D> Path;
		double Length = 0;
		double Duration = 1;
		// How far along it is, from 0 to 1.
		double T = 0;
		double Size = 1;
		bool bSea = true;
	};
	void BuildVessel(UProceduralMeshComponent* Model, int32 Kind, const FLinearColor& Main, const FLinearColor& Hull, const FLinearColor& Flag);
	UProceduralMeshComponent* NewVesselModel();
	void StartTraffic();
	void SendTraffic(FVessel& Vessel);
	void PlaceVessel(const FVessel& Vessel, double Along, double Grow);
	void DrawPlague(float DeltaSeconds);

	UPROPERTY()
	TArray<TObjectPtr<UProceduralMeshComponent>> VesselModels;

	// The everyday traffic on the routes, and the ship a house has just sent.
	TArray<FVessel> Traffic;
	FVessel Shipment;
	bool bShipping = false;
	FVector2D ShipmentAt = FVector2D::ZeroVector;
	double ShipmentStart = 0;
	TFunction<void()> ShipmentDone;

	// Voyages sailed again in the finale, several at once.
	struct FReplay { FVessel Vessel; double Start = 0; TFunction<void()> Done; };
	int32 Quiet = 0;
	TArray<FReplay> Replays;
	TArray<UProceduralMeshComponent*> SpareModels;

	// The finale's props: solid ones that cast shadows, glowing ones that do not, and the lights of candles and crown.
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Props;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlowMaterial;

	UPROPERTY()
	TArray<TObjectPtr<class UPointLightComponent>> Lamps;

	// Now and then something surfaces far out at sea: a sea serpent or a whale, as the old maps drew them.
	void DrawCreature();
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Creature;
	int32 CreatureKind = -1;
	FVector2D CreatureAt = FVector2D::ZeroVector, CreatureWay = FVector2D(1, 0);
	double CreatureStart = 0, CreatureNext = 12;

	// Stains and foul air where the plague is.
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Effects;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlazeMaterial;

	// How much of each city's stain shows (0 to 1), and whether the city is Stricken.
	TArray<double> StainAmount;
	TArray<bool> StainOn;
	bool bStainsDrawn = false;
	bool bAirDrawn = false;

	TMap<FString, FPortsCityView> CityViews;
	TArray<FPortsFloater> Floaters;
	TArray<FString> ShownHighlights;

	TArray<FPortsCityLabel> CityLabels;
	TArray<FPortsRouteBadge> RouteBadges;
	TMap<FString, TArray<FVector2D>> RouteCurves;
};
