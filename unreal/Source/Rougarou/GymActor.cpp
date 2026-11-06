#include "GymActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

const FVector AGymActor::Overview(2450, 1100, 600);
const FVector AGymActor::Focus(1300, 100, 170);

static const FLinearColor Plywood(0.75f, 0.55f, 0.33f);
static const FLinearColor Ink(0.03f, 0.03f, 0.035f);
static const FLinearColor Steel(0.04f, 0.04f, 0.045f);

// ---------------------------------------------------------------- the gym's layout, shared by the builder and the problems

// a run of wall built like a real gym section: a height profile of flat angled panels extruded along the wall
struct FWallSection
{
	// the problem set on it, if any, its moves bottom to top (j jug, c crimp, s sloper, p pinch, o pocket), and its finish height
	const TCHAR* Problem;
	const TCHAR* Moves;
	float Top;
	// floor point at the section's start, the direction along the wall, and horizontal into the room
	FVector Base;
	FVector Along;
	FVector Into;
	float Width;
	// (depth into the room, height) from the floor up, centimeters
	TArray<FVector2D> Profile;
};

static const TArray<FWallSection>& WallSections()
{
	const FVector Back(1, 0, 0), Left(0, 1, 0), Right(0, -1, 0);
	static const TArray<FWallSection> Sections = {
		// back wall: a slab, a steep wall, vertical into a kicker, a roof cave, a wave
		{ TEXT("slab dance"), TEXT("csjcps"), 4.3f, FVector(-60, -1500, 0), FVector(0, 1, 0), Back, 600, { { 0, 0 }, { -45, 200 }, { -110, 480 } } },
		{ TEXT("steep street"), TEXT("jcjpjo"), 4.6f, FVector(-60, -900, 0), FVector(0, 1, 0), Back, 600, { { 0, 0 }, { 30, 120 }, { 200, 380 }, { 250, 480 } } },
		{ TEXT("kicker"), TEXT("cjspcj"), 4.4f, FVector(-60, -300, 0), FVector(0, 1, 0), Back, 600, { { 0, 0 }, { 0, 170 }, { 110, 430 }, { 120, 480 } } },
		{ TEXT("the roof"), TEXT("jjpj"), 3.5f, FVector(-60, 300, 0), FVector(0, 1, 0), Back, 600, { { 0, 0 }, { 40, 140 }, { 230, 300 }, { 380, 380 } } },
		{ TEXT("wave"), TEXT("jcsjpc"), 4.6f, FVector(-60, 900, 0), FVector(0, 1, 0), Back, 600, { { 0, 0 }, { 90, 170 }, { 40, 320 }, { 130, 480 } } },
		// left wall: a near-vertical corner, then an overhang
		{ nullptr, nullptr, 0, FVector(-60, -1500, 0), FVector(1, 0, 0), Left, 510, { { 0, 0 }, { 0, 260 }, { -40, 480 } } },
		{ TEXT("lean"), TEXT("jcjoj"), 4.4f, FVector(450, -1500, 0), FVector(1, 0, 0), Left, 500, { { 0, 0 }, { 60, 150 }, { 180, 480 } } },
		// right wall: a steep pour, then a slab
		{ TEXT("pour"), TEXT("jpjcjj"), 4.2f, FVector(-60, 1500, 0), FVector(1, 0, 0), Right, 560, { { 0, 0 }, { 50, 160 }, { 200, 480 } } },
		{ nullptr, nullptr, 0, FVector(500, 1500, 0), FVector(1, 0, 0), Right, 550, { { 0, 0 }, { -30, 250 }, { -70, 480 } } },
	};
	return Sections;
}

// the arch boulder: a faceted cross-section extruded along the room, a thick outer skin around a cave tunnel
static const float ArchStart = 900, ArchLength = 1100, ArchCenter = 250;
static const FVector2D ArchOuter[] = { { -560, 0 }, { -470, 230 }, { -260, 470 }, { 180, 500 }, { 430, 330 }, { 560, 0 } };
static const FVector2D ArchInner[] = { { -300, 0 }, { -270, 170 }, { -120, 280 }, { 110, 290 }, { 250, 190 }, { 300, 0 } };

// Same standing-body collision samples as web/world.js. Keep these tied to
// wall profiles, not the decorative triangulation and hold meshes.
static TArray<FBox> WalkerBoxes;
bool AGymActor::WalkerBlocked(FVector At)
{
 const float Floor = At.X > -58 && At.X < 2548 && FMath::Abs(At.Y)<1498 ? 28 : 0;
 for (float Height : {35.f,100.f,165.f})
 {
  const FVector P(At.X,At.Y,Floor+Height);
  for (const FWallSection& S : WallSections())
  {
   const FVector Rel=P-S.Base;
   const float Along=FVector::DotProduct(Rel,S.Along);
   if(Along < -22 || Along > S.Width+22) continue;
   for(int32 I=0;I+1<S.Profile.Num();I++)
   {
    const FVector2D A=S.Profile[I],B=S.Profile[I+1];
    if(P.Z<A.Y || P.Z>B.Y) continue;
    const float Wall=FMath::Lerp(A.X,B.X,(P.Z-A.Y)/FMath::Max(B.Y-A.Y,.001));
    const float D=FVector::DotProduct(Rel,S.Into);
    if(D<Wall+22 && D>Wall-150) return true;
   }
  }
  if(P.X>ArchStart-22 && P.X<ArchStart+ArchLength+22)
  {
   auto Span=[&](const FVector2D* Points,float& Lo,float& Hi)
   {
    Lo=MAX_flt;Hi=-MAX_flt;
    for(int32 I=0;I<5;I++)
    {
     const FVector2D A=Points[I],B=Points[I+1];
     if(A.Y==B.Y || (P.Z-A.Y)*(P.Z-B.Y)>0)continue;
     const float X=FMath::Lerp(A.X,B.X,(P.Z-A.Y)/(B.Y-A.Y));
     Lo=FMath::Min(Lo,X);Hi=FMath::Max(Hi,X);
    }
   };
   float Lo,Hi,InnerLo,InnerHi;Span(ArchOuter,Lo,Hi);Span(ArchInner,InnerLo,InnerHi);
   const float Y=P.Y-ArchCenter;
   if(Lo<=Hi && Y>Lo-22 && Y<Hi+22 && (InnerLo>InnerHi || Y<InnerLo+22 || Y>InnerHi-22)) return true;
  }
  for(const FBox& Box:WalkerBoxes) if(Box.ExpandBy(22).IsInside(P))return true;
 }
 return At.X < -270 || At.X > 2520 || FMath::Abs(At.Y)>1618;
}
FVector AGymActor::MoveWalker(FVector From,FVector Step)
{
 for(FVector Slide : {Step,FVector(Step.X,0,0),FVector(0,Step.Y,0)})
 {
  FVector To=From+Slide;
  if(!WalkerBlocked(To)) { To.Z=To.X>-58 && To.X<2548 && FMath::Abs(To.Y)<1498 ? 28 : 0;return To; }
 }
 return From;
}
FVector AGymActor::ClearWalker(FVector At,FVector Direction)
{
 Direction.Z=0;Direction.Normalize();
 for(int32 I=0;I<100 && WalkerBlocked(At);I++)At+=Direction*5;
 At.Z=At.X>-58 && At.X<2548 && FMath::Abs(At.Y)<1498?28:0;
 return At;
}

static FVector2D ArchSection(float U, bool Inner)
{
	const FVector2D* Points = Inner ? ArchInner : ArchOuter;
	const int32 Last = UE_ARRAY_COUNT(ArchOuter) - 1;
	const float Along = FMath::Clamp(U, 0.f, 1.f) * Last;
	const int32 Segment = FMath::Clamp(FMath::FloorToInt(Along), 0, Last - 1);
	return FMath::Lerp(Points[Segment], Points[Segment + 1], Along - Segment);
}

static FVector ArchSurface(float U, float V, bool Inner)
{
	const FVector2D Point = ArchSection(U, Inner);
	return FVector(ArchStart + ArchLength * V, ArchCenter + Point.X, Point.Y);
}

// where the arch's problems sit: the outer leg climbing onto the top, and up the inner leg into the cave
static bool LegPanel(float U, float V) { return V > 0.35f && V < 0.65f && U < 0.4f + 0.001f; }
static bool CavePanel(float U, float V) { return V > 0.35f && V < 0.65f && U > 0.6f - 0.001f; }

// a stack of problem panels up a polyline of wall corners, bottom to top, facing Room
static TArray<FClimbPanel> Stack(const TArray<FVector>& Corners, FVector Across, FVector Room)
{
	TArray<FClimbPanel> Out;
	float Start = 0;
	for (int32 Index = 0; Index + 1 < Corners.Num(); Index++)
	{
		FClimbPanel Panel;
		Panel.Start = Start;
		Panel.Length = FVector::Distance(Corners[Index], Corners[Index + 1]) / 100;
		Panel.Origin = Corners[Index];
		Panel.Up = (Corners[Index + 1] - Corners[Index]).GetSafeNormal();
		Panel.Right = Across;
		Panel.Out = Panel.Right ^ Panel.Up;
		if (FVector::DotProduct(Panel.Out, Room) < 0)
		{
			Panel.Right = -Panel.Right;
			Panel.Out = Panel.Right ^ Panel.Up;
		}
		Out.Add(Panel);
		Start += Panel.Length;
	}
	return Out;
}

// a problem's holds: two start jugs for the feet, two for the hands, foot chips, its moves alternating sides, a finish jug
static TArray<FClimbHold> Ladder(const TCHAR* Moves, float Top, int32 Seed)
{
	FRandomStream Dice(Seed);
	const FVector2D Pull(0, -1);
	TArray<FClimbHold> Holds = {
		{ { -0.25f, 0.45f }, TEXT("jug"), Pull }, { { 0.25f, 0.45f }, TEXT("jug"), Pull },
		{ { -0.35f, 1.7f }, TEXT("jug"), Pull }, { { 0.35f, 1.7f }, TEXT("jug"), Pull },
		{ { 0.05f, 0.95f }, TEXT("crimp"), Pull }, { { -0.4f, 1.2f }, TEXT("crimp"), Pull }, { { 0.4f, 1.3f }, TEXT("crimp"), Pull } };
	const int32 Count = FCString::Strlen(Moves);
	for (int32 Index = 0; Index < Count; Index++)
	{
		const float Y = FMath::Lerp(2.05f, Top - 0.33f, Count > 1 ? float(Index) / (Count - 1) : 0.f);
		const float X = (Index % 2 == 0 ? -1 : 1) * Dice.FRandRange(0.2f, 0.42f);
		const TCHAR Letter = Moves[Index];
		const TCHAR* Kind = Letter == 'c' ? TEXT("crimp") : Letter == 's' ? TEXT("sloper") : Letter == 'p' ? TEXT("pinch") : Letter == 'o' ? TEXT("pocket") : TEXT("jug");
		// pinches are squeezed toward the middle of the body
		Holds.Add({ { X, Y }, Kind, Letter == 'p' ? FVector2D(X < 0 ? 1 : -1, 0) : Pull });
	}
	Holds.Add({ { 0, Top }, TEXT("jug"), Pull });
	return Holds;
}

static FTransform RouteHoldFrame(const FClimbRoute& Route, const FClimbHold& Hold)
{
	const FClimbPanel& Panel = Route.At(Hold.Point.Y);
	const FVector Up = -Hold.Pull.X * Panel.Right - Hold.Pull.Y * Panel.Up;
	return HoldFrame(Route.Map(Hold.Point), Panel.Out, Up);
}

static void SampleRouteHold(FClimbRoute& Route, int32 RouteIndex, int32 HoldIndex)
{
	FClimbHold& Hold = Route.Holds[HoldIndex];
	const int32 Template = PickTemplate(Hold.Kind, RouteIndex * 31 + HoldIndex);
	FMeshSink Resin, Mount;
	BuildHold(Template, HoldSize(Hold.Kind), FTransform::Identity, RouteIndex * 977 + HoldIndex, Resin, Mount);

	FBox Bounds(ForceInit);
	for (const FVector& Point : Resin.Vertices) Bounds += Point;
	const FTransform Frame = RouteHoldFrame(Route, Hold);
	Hold.Center = Frame.GetLocation();
	// the hold frame's z is out of the wall; x and y lie on it
	Hold.Normal = Frame.GetUnitAxis(EAxis::Z);
	Hold.Radius = FMath::Max(Bounds.GetExtent().X, Bounds.GetExtent().Y);
	Hold.Height = FMath::Max(0., Bounds.Max.Z);
	auto Surface = [&](const FVector& Start, const FVector& End) -> TOptional<FVector>
	{
		TOptional<FVector> Nearest;
		double Distance = TNumericLimits<double>::Max();
		for (int32 I = 0; I < Resin.Triangles.Num(); I += 3)
		{
			FVector Point, Normal;
			if (!FMath::SegmentTriangleIntersection(Start, End,
				Resin.Vertices[Resin.Triangles[I]], Resin.Vertices[Resin.Triangles[I + 1]],
				Resin.Vertices[Resin.Triangles[I + 2]], Point, Normal)) continue;
			const double Next = FVector::DistSquared(Start, Point);
			if (Next < Distance)
			{
				Nearest = Frame.TransformPosition(Point);
				Distance = Next;
			}
		}
		return Nearest;
	};
	// Shoes rest on the upper face. Knuckles reach the lip from the front.
	const FVector FootTop(Bounds.Max.X + 1, 0, Bounds.Max.Z * .60);
	Hold.ShoeContact = Surface(FootTop, FVector(Bounds.Min.X - 1, 0, FootTop.Z));
	const FVector HandFront(Bounds.Max.X * .60, 0, Bounds.Max.Z + 1);
	Hold.KnuckleContact = Surface(HandFront, FVector(HandFront.X, 0, -1));
}

static TArray<FClimbRoute> BuildProblems()
{
	TArray<FClimbRoute> Out;
	int32 Seed = 7;
	for (const FWallSection& Section : WallSections())
	{
		if (!Section.Problem) continue;
		TArray<FVector> Corners;
		for (const FVector2D& Point : Section.Profile)
			Corners.Add(Section.Base + Section.Along * Section.Width / 2 + Section.Into * Point.X + FVector(0, 0, Point.Y));
		FClimbRoute& Route = Out.AddDefaulted_GetRef();
		Route.Name = Section.Problem;
		Route.Panels = Stack(Corners, Section.Along, Section.Into);
		Route.Holds = Ladder(Section.Moves, Section.Top, Seed++);
	}
	// the arch: up the outer leg onto its top, and up the inner leg into the cave
	const float Middle = ArchStart + ArchLength / 2;
	auto Corner = [&](const FVector2D& Point) { return FVector(Middle, ArchCenter + Point.X, Point.Y); };
	FClimbRoute& Leg = Out.AddDefaulted_GetRef();
	Leg.Name = TEXT("arch leg");
	Leg.Panels = Stack({ Corner(ArchOuter[0]), Corner(ArchOuter[1]), Corner(ArchOuter[2]) }, FVector(1, 0, 0), FVector(0, -1, 0));
	Leg.Holds = Ladder(TEXT("jcjsjc"), 4.9f, Seed++);
	FClimbRoute& Cave = Out.AddDefaulted_GetRef();
	Cave.Name = TEXT("arch cave");
	Cave.Panels = Stack({ Corner(ArchInner[5]), Corner(ArchInner[4]), Corner(ArchInner[3]) }, FVector(1, 0, 0), FVector(0, -1, 0));
	Cave.Holds = Ladder(TEXT("jjjj"), 2.95f, Seed++);
	Cave.Stand = 200;
	Cave.Aside = 420;
	for (int32 Route = 0; Route < Out.Num(); ++Route)
		for (int32 Hold = 0; Hold < Out[Route].Holds.Num(); ++Hold)
			SampleRouteHold(Out[Route], Route, Hold);
	return Out;
}

TArray<FClimbRoute> AGymActor::Problems()
{
	static const TArray<FClimbRoute> Routes = BuildProblems();
	return Routes;
}


FLinearColor AGymActor::Tape(int32 Index)
{
	static const FLinearColor Colors[] = {
		FLinearColor(0.95f, 0.62f, 0.02f), FLinearColor(0.02f, 0.25f, 0.8f), FLinearColor(0.05f, 0.45f, 0.15f), FLinearColor(0.8f, 0.05f, 0.05f),
		FLinearColor(0.45f, 0.12f, 0.75f), FLinearColor(0.95f, 0.35f, 0.02f), FLinearColor(0.9f, 0.3f, 0.55f), FLinearColor(0.02f, 0.55f, 0.6f),
		FLinearColor(0.85f, 0.85f, 0.82f), FLinearColor(0.35f, 0.75f, 0.1f) };
	return Colors[Index % UE_ARRAY_COUNT(Colors)];
}

// ---------------------------------------------------------------- building it

AGymActor::AGymActor()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Room"));
	Panels = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Panels"));
	Panels->SetupAttachment(RootComponent);
	Panels->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HoldMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Holds"));
	HoldMesh->SetupAttachment(RootComponent);
	HoldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AGymActor::BeginPlay()
{
	WalkerBoxes.Reset();
	Super::BeginPlay();
	Dice.Initialize(1991);
	Shell();
	Walls();
	Arch();
	Loft();
	Garage();
	const TArray<FClimbRoute> Routes = Problems();
	for (int32 Index = 0; Index < Routes.Num(); Index++) Set(Routes[Index], Index);
	const FLinearColor Sections[2] = { Plywood, Ink };
	const float Grain[2] = { 1.f, 0.35f };
	for (int32 Section = 0; Section < 2; Section++)
	{
		Panels->CreateMeshSection_LinearColor(Section, Boards[Section].Vertices, Boards[Section].Triangles, Boards[Section].Normals, {}, {}, {}, false);
		Panels->SetMaterial(Section, Finish(TEXT("/Game/Gym/M_Panel.M_Panel"), Sections[Section], Grain[Section], Section == 0 ? 0.62f : 0.55f));
	}
	UMaterialInterface* Grit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Gym/M_HoldGrit.M_HoldGrit"));
	for (int32 Color = 0; Color < Paint.Num(); Color++)
	{
		HoldMesh->CreateMeshSection_LinearColor(Color, Paint[Color].Vertices, Paint[Color].Triangles, Paint[Color].Normals, {}, {}, {}, false);
		UMaterialInstanceDynamic* Finish = Grit ? UMaterialInstanceDynamic::Create(Grit, this) : Coat(Palette[Color]);
		Finish->SetVectorParameterValue(TEXT("Color"), Palette[Color]);
		HoldMesh->SetMaterial(Color, Finish);
	}
	HoldMesh->CreateMeshSection_LinearColor(Paint.Num(), Shadow.Vertices, Shadow.Triangles, Shadow.Normals, {}, {}, {}, false);
	HoldMesh->SetMaterial(Paint.Num(), Coat(FLinearColor(0.01f, 0.01f, 0.01f)));
	// Procedural mesh sections own their vertex and index data after upload.
	for (FMeshSink& Board : Boards) Board = {};
	Paint.Empty();
	Shadow = {};
}

UMaterialInstanceDynamic* AGymActor::Finish(const TCHAR* Path, FLinearColor Color, float Grain, float Roughness)
{
	// world-aligned authored surface from unreal/Tools/build_gym_materials.py, falling back to the flat coat
	UMaterialInterface* Surface = LoadObject<UMaterialInterface>(nullptr, Path);
	if (!Surface) return Coat(Color);
	UMaterialInstanceDynamic* Layer = UMaterialInstanceDynamic::Create(Surface, this);
	Layer->SetVectorParameterValue(TEXT("Color"), Color);
	Layer->SetScalarParameterValue(TEXT("Grain"), Grain);
	Layer->SetScalarParameterValue(TEXT("Roughness"), Roughness);
	return Layer;
}

UMaterialInstanceDynamic* AGymActor::Coat(FLinearColor Color)
{
	UMaterialInstanceDynamic* Layer = UMaterialInstanceDynamic::Create(LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")), this);
	Layer->SetVectorParameterValue(TEXT("Color"), Color);
	return Layer;
}

int32 AGymActor::Swatch(FLinearColor Color)
{
	const int32 Found = Palette.IndexOfByKey(Color);
	if (Found != INDEX_NONE) return Found;
	Paint.AddDefaulted();
	return Palette.Add(Color);
}

UInstancedStaticMeshComponent* AGymActor::Batch(FName Name, const TCHAR* Mesh, FLinearColor Color, bool Shadows)
{
	if (TObjectPtr<UInstancedStaticMeshComponent>* Found = Batches.Find(Name)) return *Found;
	UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(this);
	Instances->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
	Instances->SetupAttachment(RootComponent);
	Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Instances->SetCastShadow(Shadows);
	Instances->SetMaterial(0, Coat(Color));
	Instances->RegisterComponent();
	Batches.Add(Name, Instances);
	return Instances;
}

void AGymActor::Block(FVector Center, FVector Size, FLinearColor Color, bool Shadows)
{
	if(Center.Z-Size.Z/2<200 && Center.Z+Size.Z/2>35) WalkerBoxes.Add(FBox(Center-Size/2,Center+Size/2));
	// one batch per color keeps the blockout to a handful of draw calls; the basic cube is 100cm
	const FName Name(*FString::Printf(TEXT("block %s %d"), *Color.ToString(), Shadows));
	Batch(Name, TEXT("/Engine/BasicShapes/Cube.Cube"), Color, Shadows)->AddInstance(FTransform(FRotator::ZeroRotator, Center, Size / 100));
}

void AGymActor::Facets(TFunctionRef<FVector(float, float)> Surface, int32 Columns, int32 Rows, float Bump, float Dark, float Crowd,
	TFunctionRef<bool(float, float)> Flat, TFunctionRef<bool(FVector, FVector)> Show)
{
	// a grid over the surface split into triangles, each plywood or black: the faceted panel look. inner points can be
	// pushed in or out for texture; edges and problem areas stay on the surface so neighbors meet and holds sit flat
	TArray<FVector> Points;
	for (int32 Row = 0; Row <= Rows; Row++)
		for (int32 Column = 0; Column <= Columns; Column++)
		{
			const float U = float(Column) / Columns, V = float(Row) / Rows;
			const FVector Point = Surface(U, V);
			const FVector Across = Surface(FMath::Min(U + 0.01f, 1.f), V) - Surface(FMath::Max(U - 0.01f, 0.f), V);
			const FVector Up = Surface(U, FMath::Min(V + 0.01f, 1.f)) - Surface(U, FMath::Max(V - 0.01f, 0.f));
			const bool Edge = Column == 0 || Column == Columns || Row == 0 || Row == Rows || Flat(U, V);
			Points.Add(Point + (Across ^ Up).GetSafeNormal() * (Edge ? 0 : Dice.FRandRange(-Bump, Bump)));
		}
	auto At = [&](int32 Column, int32 Row) { return Points[Row * (Columns + 1) + Column]; };
	for (int32 Row = 0; Row < Rows; Row++)
		for (int32 Column = 0; Column < Columns; Column++)
		{
			const FVector A = At(Column, Row), B = At(Column + 1, Row), C = At(Column + 1, Row + 1), D = At(Column, Row + 1);
			const bool Flip = (Row + Column) % 2 == 0;
			const bool Clear = Flat((Column + 0.5f) / Columns, (Row + 0.5f) / Rows);
			const FVector Corners[2][3] = { { A, B, Flip ? C : D }, { Flip ? A : B, C, D } };
			for (const auto& Corner : Corners)
			{
				const FVector Normal = ((Corner[1] - Corner[0]) ^ (Corner[2] - Corner[0])).GetSafeNormal();
				FMeshSink& Board = Boards[Dice.FRand() < Dark ? 1 : 0];
				Board.Add(Corner[0], Corner[1], Corner[2], Normal);
				Board.Add(Corner[0], Corner[1], Corner[2], -Normal);
				if (Clear) continue;
				const FVector Middle = (Corner[0] + Corner[1] + Corner[2]) / 3;
				for (const FVector Side : { Normal, -Normal })
					if (Show(Middle, Side)) Sprinkle(Corner[0], Corner[1], Corner[2], Side, Crowd);
			}
		}
}

void AGymActor::Sprinkle(FVector A, FVector B, FVector C, FVector Normal, float Crowd)
{
	// set decor holds on one side of a facet: the whole library, mostly hand-sized, some big, some turned, the odd volume
	static const FLinearColor Colors[] = {
		FLinearColor(0.75f, 0.03f, 0.03f), FLinearColor(0.95f, 0.62f, 0.02f), FLinearColor(0.02f, 0.18f, 0.75f), FLinearColor(0.04f, 0.36f, 0.14f),
		FLinearColor(0.32f, 0.1f, 0.55f), FLinearColor(0.22f, 0.24f, 0.27f), FLinearColor(0.9f, 0.3f, 0.03f), FLinearColor(0.75f, 0.75f, 0.72f) };
	const TArray<FHoldTemplate>& Library = HoldTemplates();
	const int32 Count = FMath::FloorToInt(Crowd + Dice.FRand());
	for (int32 Index = 0; Index < Count; Index++)
	{
		float S = Dice.FRand(), T = Dice.FRand();
		if (S + T > 1) { S = 1 - S; T = 1 - T; }
		int32 Template = Dice.RandRange(0, Library.Num() - 1);
		if (FString(Library[Template].Kind) == TEXT("volume")) Template = 0;
		// most holds face up the wall, some are turned for sidepulls and underclings
		const FVector Up = FVector(0, 0, 1).RotateAngleAxis(Dice.FRand() < 0.7f ? Dice.FRandRange(-35, 35) : Dice.FRandRange(0, 360), Normal);
		const float Size = HoldSize(Library[Template].Kind) * (Dice.FRand() < 0.07f ? Dice.FRandRange(1.6f, 2.3f) : Dice.FRandRange(0.8f, 1.2f));
		const int32 Color = Swatch(Colors[Dice.RandRange(0, UE_ARRAY_COUNT(Colors) - 1)]);
		BuildHold(Template, Size, HoldFrame(A + (B - A) * S + (C - A) * T, Normal, Up), Dice.RandRange(0, 1 << 20), Paint[Color], Shadow);
	}
	if (Dice.FRand() < 0.035f * Crowd)
	{
		const bool Black = Dice.FRand() < 0.6f;
		const FVector Up = FVector(0, 0, 1).RotateAngleAxis(Dice.FRandRange(0, 360), Normal);
		const int32 Color = Swatch(Black ? FLinearColor(0.02f, 0.02f, 0.025f) : FLinearColor(0.2f, 0.21f, 0.23f));
		BuildHold(PickTemplate(TEXT("volume"), Dice.RandRange(0, 9)), Dice.FRandRange(45, 75), HoldFrame((A + B + C) / 3, Normal, Up), Dice.RandRange(0, 1 << 20), Paint[Color], Shadow);
	}
}

void AGymActor::Set(const FClimbRoute& Problem, int32 Index)
{
	// a problem's holds in its tape color, turned so each lip faces away from the way it is pulled
	const int32 Color = Swatch(Tape(Index));
	for (int32 Hold = 0; Hold < Problem.Holds.Num(); Hold++)
	{
		const FClimbHold& Spot = Problem.Holds[Hold];
		BuildHold(PickTemplate(Spot.Kind, Index * 31 + Hold), HoldSize(Spot.Kind), RouteHoldFrame(Problem, Spot), Index * 977 + Hold, Paint[Color], Shadow);
	}
}

void AGymActor::Shell()
{
	// black mat tiles with seams, over a concrete slab
	const FLinearColor Mat(0.03f, 0.03f, 0.035f);
	for (float X = 100; X < 2450; X += 200)
		for (float Y = -1450; Y < 1500; Y += 100)
			Batch(TEXT("mat"), TEXT("/Engine/BasicShapes/Cube.Cube"), Mat)->AddInstance(FTransform(FRotator::ZeroRotator, FVector(X, Y, 14), FVector(1.96f, 0.97f, 0.28f)));
	// a row of narrow pads tight against the back wall, so nobody lands on bare concrete
	for (float Y = -1450; Y < 1500; Y += 100)
		Batch(TEXT("mat"), TEXT("/Engine/BasicShapes/Cube.Cube"), Mat)->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-29, Y, 14), FVector(0.58f, 0.97f, 0.28f)));
	Batch(TEXT("mat"), TEXT("/Engine/BasicShapes/Cube.Cube"), Mat)->SetMaterial(0, Finish(TEXT("/Game/Gym/M_Rubber.M_Rubber"), Mat, 0, 0.8f));
	Block(FVector(1150, 0, -5), FVector(2950, 3400, 10), FLinearColor(0.25f, 0.25f, 0.25f), false);
	// the steel building: shell walls, insulated ceiling, beams, purlins. none of it casts shadow, or the roof would black out the key light
	const FLinearColor Siding(0.6f, 0.6f, 0.58f);
	Block(FVector(-300, 0, 450), FVector(20, 3500, 900), Siding, false);
	Block(FVector(1150, -1650, 450), FVector(2900, 20, 900), Siding, false);
	Block(FVector(1150, 1650, 450), FVector(2900, 20, 900), Siding, false);
	Block(FVector(2560, 0, 450), FVector(20, 3300, 900), Siding, false);
	Block(FVector(1150, 0, 915), FVector(2950, 3400, 20), FLinearColor(0.72f, 0.66f, 0.55f), false);
	for (float X = -200; X <= 2400; X += 520)
		Block(FVector(X, 0, 870), FVector(30, 3300, 60), Steel, false);
	for (float Y = -1200; Y <= 1200; Y += 600)
		Block(FVector(1150, Y, 896), FVector(2900, 14, 14), Steel, false);
	// led strips with a soft light under each
	for (float X = 300; X <= 2100; X += 600)
		for (float Y = -900; Y <= 900; Y += 600)
		{
			Block(FVector(X, Y, 852), FVector(240, 14, 5), FLinearColor(1, 1, 1), false);
			UPointLightComponent* Strip = NewObject<UPointLightComponent>(this);
			Strip->SetupAttachment(RootComponent);
			Strip->SetRelativeLocation(FVector(X, Y, 790));
			Strip->SetIntensity(9000);
			Strip->SetAttenuationRadius(2200);
			Strip->SetCastShadows(false);
			Strip->RegisterComponent();
		}
}

void AGymActor::Walls()
{
	// each section: facet its profile, rows following the fold lines so every angled panel stays a clean plane
	const TArray<FWallSection>& Sections = WallSections();
	for (const FWallSection& Section : Sections)
	{
		const int32 Folds = Section.Profile.Num() - 1;
		TArray<int32> Rows;
		int32 Total = 0;
		for (int32 Fold = 0; Fold < Folds; Fold++)
		{
			Rows.Add(FMath::Max(1, FMath::RoundToInt(FVector2D::Distance(Section.Profile[Fold], Section.Profile[Fold + 1]) / 120)));
			Total += Rows.Last();
		}
		auto Height = [&](float Row)
		{
			for (int32 Fold = 0; Fold < Folds; Fold++)
			{
				if (Row <= Rows[Fold] || Fold == Folds - 1)
					return FMath::Lerp(Section.Profile[Fold], Section.Profile[Fold + 1], FMath::Clamp(Row / Rows[Fold], 0.f, 1.f));
				Row -= Rows[Fold];
			}
			return Section.Profile.Last();
		};
		auto Surface = [&](float U, float V)
		{
			const FVector2D Point = Height(V * Total);
			return Section.Base + Section.Along * Section.Width * U + Section.Into * Point.X + FVector(0, 0, Point.Y);
		};
		const bool Taped = Section.Problem != nullptr;
		auto Flat = [&](float U, float) { return Taped && FMath::Abs(U - 0.5f) * Section.Width < 95; };
		auto Show = [&](FVector, FVector Normal) { return FVector::DotProduct(Normal, Section.Into) > 0.1f; };
		Facets(Surface, FMath::Max(3, FMath::RoundToInt(Section.Width / 130)), Total, 16, 0.35f, 1.6f, Flat, Show);
	}
	// returns: side panels closing the gap wherever two profiles meet, and at the ends of each wall
	auto Resample = [](const TArray<FVector2D>& Profile, float T)
	{
		float Length = 0;
		for (int32 Index = 0; Index + 1 < Profile.Num(); Index++) Length += FVector2D::Distance(Profile[Index], Profile[Index + 1]);
		float Goal = T * Length;
		for (int32 Index = 0; Index + 1 < Profile.Num(); Index++)
		{
			const float Piece = FVector2D::Distance(Profile[Index], Profile[Index + 1]);
			if (Goal <= Piece || Index + 2 == Profile.Num()) return FMath::Lerp(Profile[Index], Profile[Index + 1], FMath::Clamp(Goal / FMath::Max(Piece, 1.f), 0.f, 1.f));
			Goal -= Piece;
		}
		return Profile.Last();
	};
	auto Return = [&](FVector Base, FVector Into, const TArray<FVector2D>& One, const TArray<FVector2D>& Two, FVector Along)
	{
		const int32 Steps = 16;
		for (int32 Step = 0; Step < Steps; Step++)
		{
			auto Place = [&](const TArray<FVector2D>& Profile, float T) { const FVector2D P = Resample(Profile, T); return Base + Into * P.X + FVector(0, 0, P.Y); };
			const float T0 = float(Step) / Steps, T1 = float(Step + 1) / Steps;
			const FVector A = Place(One, T0), B = Place(One, T1), C = Place(Two, T1), D = Place(Two, T0);
			for (const FVector Side : { Along, -Along })
			{
				Boards[0].Add(A, B, C, Side);
				Boards[0].Add(A, C, D, Side);
			}
		}
	};
	for (int32 Index = 0; Index < Sections.Num(); Index++)
	{
		const FWallSection& Section = Sections[Index];
		const TArray<FVector2D> Plain = { { 0, 0 }, { 0, Section.Profile.Last().Y } };
		const bool First = Index == 0 || Sections[Index - 1].Along != Section.Along || Sections[Index - 1].Into != Section.Into;
		const bool Last = Index + 1 == Sections.Num() || Sections[Index + 1].Along != Section.Along || Sections[Index + 1].Into != Section.Into;
		if (First) Return(Section.Base, Section.Into, Section.Profile, Plain, Section.Along);
		const FVector End = Section.Base + Section.Along * Section.Width;
		Return(End, Section.Into, Section.Profile, Last ? Plain : Sections[Index + 1].Profile, Section.Along);
	}
}

void AGymActor::Arch()
{
	// a massive boulder: thick faceted outer skin, a cave tunnel through it, faceted end faces between the two
	const int32 Columns = 15, Rows = 10;
	auto Outside = [](FVector Point, FVector Normal) { return FVector::DotProduct(Normal, Point - FVector(Point.X, ArchCenter, 150)) > 0; };
	auto Inside = [](FVector Point, FVector Normal) { return FVector::DotProduct(Normal, Point - FVector(Point.X, ArchCenter, 150)) < 0; };
	Facets([](float U, float V) { return ArchSurface(U, V, false); }, Columns, Rows, 30, 0.4f, 1.3f, LegPanel, Outside);
	Facets([](float U, float V) { return ArchSurface(U, V, true); }, Columns, Rows, 22, 0.45f, 1.6f, CavePanel, Inside);
	auto Solid = [](float, float) { return false; };
	for (const float V : { 0.f, 1.f })
	{
		const float X = ArchStart + ArchLength * V;
		const FVector Face(V == 0 ? -1 : 1, 0, 0);
		Facets([=](float U, float T) { const FVector2D P = FMath::Lerp(ArchSection(U, true), ArchSection(U, false), T); return FVector(X, ArchCenter + P.X, P.Y); },
			Columns, 3, 18, 0.4f, 1.4f, Solid, [=](FVector, FVector Normal) { return FVector::DotProduct(Normal, Face) > 0.1f; });
	}
	// the arch shadows the key light, so the cave gets its own soft spill
	for (const float V : { 0.25f, 0.75f })
	{
		UPointLightComponent* Spill = NewObject<UPointLightComponent>(this);
		Spill->SetupAttachment(RootComponent);
		Spill->SetRelativeLocation(FVector(ArchStart + ArchLength * V, ArchCenter, 150));
		Spill->SetIntensity(4500);
		Spill->SetAttenuationRadius(900);
		Spill->SetCastShadows(false);
		Spill->RegisterComponent();
	}
}

void AGymActor::Loft()
{
	// the lounge on the right: wood lower wall with a door, a mezzanine with railing, tvs on the back wall, plants, warm lights
	const FLinearColor Wood(0.62f, 0.42f, 0.24f), Dark(0.11f, 0.09f, 0.07f);
	Block(FVector(1600, 1515, 170), FVector(1000, 12, 340), Wood);
	Block(FVector(1300, 1507, 105), FVector(95, 6, 210), FLinearColor(0.85f, 0.85f, 0.84f));
	Block(FVector(1600, 1515, 560), FVector(1000, 12, 420), Dark);
	Block(FVector(1600, 1340, 345), FVector(1000, 360, 30), Steel);
	Block(FVector(1600, 1340, 700), FVector(1000, 360, 14), Wood, false);
	for (float X = 1350; X <= 1850; X += 250)
	{
		Block(FVector(X, 1506, 560), FVector(165, 5, 98), FLinearColor(0.01f, 0.01f, 0.01f));
		Block(FVector(X, 1502, 560), FVector(150, 4, 85), FLinearColor(0.08f, 0.4f, 0.12f));
	}
	for (float X = 1100; X <= 2100; X += 50) Block(FVector(X, 1162, 405), FVector(4, 4, 90), Steel);
	Block(FVector(1600, 1162, 452), FVector(1000, 6, 6), Steel);
	Block(FVector(1600, 1162, 400), FVector(1000, 4, 4), Steel);
	Block(FVector(1100, 1162, 165), FVector(25, 25, 330), Steel);
	Block(FVector(2100, 1162, 165), FVector(25, 25, 330), Steel);
	for (const float X : { 1180.f, 2020.f })
	{
		Block(FVector(X, 1450, 385), FVector(40, 40, 50), Dark);
		Batch(TEXT("plant"), TEXT("/Engine/BasicShapes/Sphere.Sphere"), FLinearColor(0.05f, 0.25f, 0.06f))->AddInstance(FTransform(FRotator::ZeroRotator, FVector(X, 1450, 455), FVector(0.7f, 0.7f, 0.9f)));
	}
	// lounge chairs under the loft
	for (float X = 1450; X <= 1950; X += 125)
		Block(FVector(X, 1400, 55), FVector(60, 60, 50), FLinearColor(0.5f, 0.38f, 0.26f));
	for (float X = 1350; X <= 1850; X += 250)
	{
		UPointLightComponent* Warm = NewObject<UPointLightComponent>(this);
		Warm->SetupAttachment(RootComponent);
		Warm->SetRelativeLocation(FVector(X, 1340, 680));
		Warm->SetLightColor(FLinearColor(1, 0.75f, 0.45f));
		Warm->SetIntensity(1800);
		Warm->SetAttenuationRadius(650);
		Warm->SetCastShadows(false);
		Warm->RegisterComponent();
	}
}

void AGymActor::Garage()
{
	// the roll-up glass door on the left wall, with daylight coming through
	const FLinearColor Frame(0.8f, 0.8f, 0.8f);
	Block(FVector(1350, -1545, 230), FVector(700, 8, 460), FLinearColor(0.78f, 0.86f, 0.92f), false);
	for (float Z = 0; Z <= 460; Z += 76) Block(FVector(1350, -1538, Z), FVector(700, 6, 6), Frame, false);
	for (float X = 1000; X <= 1700; X += 100) Block(FVector(X, -1538, 230), FVector(6, 6, 460), Frame, false);
	UPointLightComponent* Day = NewObject<UPointLightComponent>(this);
	Day->SetupAttachment(RootComponent);
	Day->SetRelativeLocation(FVector(1350, -1350, 260));
	Day->SetLightColor(FLinearColor(0.8f, 0.9f, 1));
	Day->SetIntensity(3000);
	Day->SetAttenuationRadius(1400);
	Day->SetCastShadows(false);
	Day->RegisterComponent();
}
