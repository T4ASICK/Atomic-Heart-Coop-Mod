#include "AHGhostNavigationBuildCommandlet.h"
#include "AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/ISlateRHIRendererModule.h"
#include "Modules/ModuleManager.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "UObject/UnrealType.h"
#include "Engine/Font.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "WidgetBlueprint.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace
{
bool SaveAsset(UPackage* Package,UObject* Asset)
{
    FAssetRegistryModule::AssetCreated(Asset);
    Package->MarkPackageDirty();
    const FString File=FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
    return UPackage::SavePackage(Package,Asset,RF_Public|RF_Standalone,*File,GError,nullptr,false,true,SAVE_NoError);
}
bool BuildMenu()
{
    UPackage* Package=CreatePackage(TEXT("/Game/Mods/AHGhostCoopNavigation/WBP_GhostMenu"));
    UWidgetBlueprint* Widget=CastChecked<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UUserWidget::StaticClass(),Package,TEXT("WBP_GhostMenu"),BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass()));
    FEdGraphPinType Integer; Integer.PinCategory=UEdGraphSchema_K2::PC_Int;
    FBlueprintEditorUtils::AddMemberVariable(Widget,TEXT("Request"),Integer,TEXT("0"));
    UBorder* Root=Widget->WidgetTree->ConstructWidget<UBorder>();
    Root->SetBrushColor(FLinearColor(.025f,.035f,.05f,.98f)); Root->SetPadding(FMargin(16));
    USizeBox* Frame=Widget->WidgetTree->ConstructWidget<USizeBox>();
    Frame->SetWidthOverride(600); Frame->SetHeightOverride(640); Frame->AddChild(Root);
    Widget->WidgetTree->RootWidget=Frame;
    UVerticalBox* Rows=Widget->WidgetTree->ConstructWidget<UVerticalBox>(); Root->AddChild(Rows);
    UObject* Font=LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    auto Text=[&](const FString& Name,const FString& Label,int32 Size)
    {
        UTextBlock* Block=Widget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(*Name));
        Block->bIsVariable=true; Block->SetFont(FSlateFontInfo(Font,Size,TEXT("Regular")));
        Block->SetText(FText::FromString(Label)); Block->SetAutoWrapText(true);
        return Block;
    };
    Rows->AddChildToVerticalBox(Text(TEXT("Title"),TEXT("AH Ghost Co-op · 8 игроков"),24))->SetPadding(FMargin(0,0,0,6));
    USizeBox* StatusBox=Widget->WidgetTree->ConstructWidget<USizeBox>();
    StatusBox->SetHeightOverride(56); StatusBox->SetClipping(EWidgetClipping::ClipToBounds);
    StatusBox->AddChild(Text(TEXT("StatusText"),TEXT("Steam: 1/8 · создайте лобби или примите приглашение"),14));
    Rows->AddChildToVerticalBox(StatusBox);
    TArray<UButton*> Buttons; TArray<int32> ActionIds;
    auto Button=[&](const FString& Name,const FString& Label,int32 Action)
    {
        auto* Value=Widget->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),FName(*Name));
        Value->bIsVariable=true; Value->SetBackgroundColor(FLinearColor(.10f,.18f,.23f,1));
        auto* Caption=Text(Action>=100 ? FString::Printf(TEXT("ItemLabel%d"),Action-100) : TEXT("Label")+Name,Label,16);
        Caption->SetAutoWrapText(false); Caption->SetJustification(ETextJustify::Center);
        auto* Slot=CastChecked<UButtonSlot>(Value->AddChild(Caption));
        Slot->SetHorizontalAlignment(HAlign_Fill); Slot->SetVerticalAlignment(VAlign_Center); Slot->SetPadding(FMargin(8,3));
        Buttons.Add(Value); ActionIds.Add(Action); return Value;
    };
    UHorizontalBox* Tabs=Widget->WidgetTree->ConstructWidget<UHorizontalBox>();
    const TCHAR* TabLabels[]={TEXT("Steam"),TEXT("LAN"),TEXT("Телепортация")};
    for (int32 Index=0;Index<3;++Index)
    {
        auto* Slot=Tabs->AddChildToHorizontalBox(Button(FString::Printf(TEXT("Action%d"),21+Index),TabLabels[Index],21+Index));
        Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); Slot->SetPadding(FMargin(2,0));
    }
    USizeBox* TabFrame=Widget->WidgetTree->ConstructWidget<USizeBox>(); TabFrame->SetHeightOverride(36); TabFrame->AddChild(Tabs);
    Rows->AddChildToVerticalBox(TabFrame)->SetPadding(FMargin(0,3));
    Rows->AddChildToVerticalBox(Text(TEXT("PageTitle"),TEXT("Steam Spacewar"),18))->SetPadding(FMargin(0,3));
    auto* Inputs=Widget->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("Inputs"));
    Inputs->bIsVariable=true; Rows->AddChildToVerticalBox(Inputs);
    const TCHAR* InputNames[]={TEXT("NameInput"),TEXT("RoomInput"),TEXT("AddressInput")};
    const TCHAR* Hints[]={TEXT("Ваше имя"),TEXT("Код комнаты (необязательно для Steam)"),TEXT("ID лобби Steam / адрес LAN-сервера")};
    for (int32 Index=0;Index<3;++Index)
    {
        auto* Input=Widget->WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(),InputNames[Index]);
        Input->bIsVariable=true; Input->WidgetStyle.SetFont(FSlateFontInfo(Font,15,TEXT("Regular")));
        Input->SetHintText(FText::FromString(Hints[Index]));
        if (!Index) Input->SetText(FText::FromString(TEXT("Player")));
        auto* Box=Widget->WidgetTree->ConstructWidget<USizeBox>(); Box->SetHeightOverride(32); Box->AddChild(Input);
        Inputs->AddChildToVerticalBox(Box)->SetPadding(FMargin(0,1));
    }
    const TCHAR* Labels[]={TEXT("Создать лобби"),TEXT("Подключиться по ID лобби"),
        TEXT("Пригласить друга — Steam Overlay"),TEXT("Покинуть лобби"),TEXT("Показать / скрыть игроков"),TEXT(""),TEXT("")};
    for (int32 Index=0;Index<7;++Index)
    {
        auto* Box=Widget->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),*FString::Printf(TEXT("ItemRow%d"),Index));
        Box->bIsVariable=true; Box->SetHeightOverride(36);
        Box->AddChild(Button(FString::Printf(TEXT("Item%d"),Index),Labels[Index],100+Index));
        if (Index>=5) Box->SetVisibility(ESlateVisibility::Collapsed);
        Rows->AddChildToVerticalBox(Box)->SetPadding(FMargin(0,1));
    }
    auto* Close=Widget->WidgetTree->ConstructWidget<USizeBox>(); Close->SetHeightOverride(36);
    Close->AddChild(Button(TEXT("Action1"),TEXT("Закрыть меню · Home"),1));
    Rows->AddChildToVerticalBox(Close)->SetPadding(FMargin(0,4,0,0));
    FKismetEditorUtilities::CompileBlueprint(Widget);
    UEdGraph* Graph=FBlueprintEditorUtils::FindEventGraph(Widget);
    if (!Graph) return false;
    auto* Schema=CastChecked<UEdGraphSchema_K2>(Graph->GetSchema());
    for (int32 Index=0;Index<Buttons.Num();++Index)
    {
        auto* Property=FindFProperty<FObjectProperty>(Widget->GeneratedClass,Buttons[Index]->GetFName());
        auto* Delegate=FindFProperty<FMulticastDelegateProperty>(UButton::StaticClass(),TEXT("OnClicked"));
        if (!Property || !Delegate) return false;
        auto* Event=NewObject<UK2Node_ComponentBoundEvent>(Graph);
        Graph->AddNode(Event,true,false); Event->CreateNewGuid();
        Event->InitializeComponentBoundEventParams(Property,Delegate); Event->AllocateDefaultPins();
        auto* Set=NewObject<UK2Node_VariableSet>(Graph);
        Graph->AddNode(Set,true,false); Set->CreateNewGuid(); Set->VariableReference.SetSelfMember(TEXT("Request")); Set->AllocateDefaultPins();
        Set->FindPinChecked(TEXT("Request"))->DefaultValue=FString::FromInt(ActionIds[Index]);
        if (!Schema->TryCreateConnection(Event->FindPinChecked(UEdGraphSchema_K2::PN_Then),Set->FindPinChecked(UEdGraphSchema_K2::PN_Execute))) return false;
    }
    FKismetEditorUtilities::CompileBlueprint(Widget);
    if (Widget->Status==BS_Error) return false;
    UUserWidget* Instance=NewObject<UUserWidget>(GetTransientPackage(),Widget->GeneratedClass);
    if (!Instance->Initialize()) return false;
    auto* Request=FindFProperty<FIntProperty>(Instance->GetClass(),TEXT("Request"));
    if (!Request) return false;
    for (int32 Index=0;Index<Buttons.Num();++Index)
    {
        auto* Value=Cast<UButton>(Instance->GetWidgetFromName(Buttons[Index]->GetFName()));
        if (!Value) return false;
        Request->SetPropertyValue_InContainer(Instance,0); Value->OnClicked.Broadcast();
        if (Request->GetPropertyValue_InContainer(Instance)!=ActionIds[Index]) return false;
    }
    for (const TCHAR* Name : InputNames)
        if (!Cast<UEditableTextBox>(Instance->GetWidgetFromName(Name))) return false;
    FWidgetRenderer Renderer(true);
    auto SlateWidget=Instance->TakeWidget();
    auto* Target=Renderer.DrawWidget(SlateWidget,FVector2D(600,640)); FlushRenderingCommands();
    TArray<FColor> Pixels;
    if (!Target || !Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)) return false;
    TArray<uint8> PNG; FImageUtils::CompressImageArray(600,640,Pixels,PNG);
    if (!FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("GhostMenu-preview.png")))) return false;
    for (auto* Source : Buttons)
    {
        auto* Value=Instance->GetWidgetFromName(Source->GetFName());
        const auto Geometry=Value->GetPaintSpaceGeometry();
        if (Geometry.GetLocalSize().Y>0 && Geometry.GetAbsolutePosition().Y+Geometry.GetAbsoluteSize().Y>640.5f)
        { UE_LOG(LogTemp,Error,TEXT("Lobby menu overflow: %s"),*Value->GetName()); return false; }
    }
    Instance->GetWidgetFromName(TEXT("Inputs"))->SetVisibility(ESlateVisibility::Collapsed);
    CastChecked<UTextBlock>(Instance->GetWidgetFromName(TEXT("PageTitle")))->SetText(FText::FromString(TEXT("Телепортация — выберите игрока")));
    CastChecked<UTextBlock>(Instance->GetWidgetFromName(TEXT("StatusText")))->SetText(FText::FromString(TEXT("Steam: 8/8 · выберите напарника для телепортации")));
    for (int32 Index=0;Index<7;++Index)
    {
        Instance->GetWidgetFromName(*FString::Printf(TEXT("ItemRow%d"),Index))->SetVisibility(ESlateVisibility::Visible);
        CastChecked<UTextBlock>(Instance->GetWidgetFromName(*FString::Printf(TEXT("ItemLabel%d"),Index)))->SetText(
            FText::FromString(FString::Printf(TEXT("Игрок %d%s"),Index+1,Index==6 ? TEXT(" — другая карта") : TEXT(""))));
        Instance->GetWidgetFromName(*FString::Printf(TEXT("Item%d"),Index))->SetIsEnabled(Index!=6);
    }
    Instance->ForceLayoutPrepass();
    Target=Renderer.DrawWidget(SlateWidget,FVector2D(600,640)); FlushRenderingCommands();
    Pixels.Reset(); PNG.Reset();
    if (!Target || !Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)) return false;
    FImageUtils::CompressImageArray(600,640,Pixels,PNG);
    if (!FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("GhostPlayers-preview.png")))) return false;
    for (int32 Index=0;Index<7;++Index)
    {
        const auto Geometry=Instance->GetWidgetFromName(*FString::Printf(TEXT("Item%d"),Index))->GetPaintSpaceGeometry();
        if (Geometry.GetLocalSize().Y<30 || Geometry.GetAbsolutePosition().Y+Geometry.GetAbsoluteSize().Y>640)
        { UE_LOG(LogTemp,Error,TEXT("Teleport list overflow: row %d"),Index); return false; }
    }
    UE_LOG(LogTemp,Display,TEXT("Lobby menu self-test: all 11 button events and 3 editable inputs verified; rendered 600x640"));
    return SaveAsset(Package,Widget);
}
bool BuildOpaqueWeapon()
{
    UPackage* Package=CreatePackage(TEXT("/Game/Mods/AHGhostCoopNavigation/M_GhostWeaponOpaque"));
    UMaterial* Material=NewObject<UMaterial>(Package,TEXT("M_GhostWeaponOpaque"),RF_Public|RF_Standalone);
    Material->BlendMode=BLEND_Opaque; Material->bDisableDepthTest=false;
    auto* Color=NewObject<UMaterialExpressionConstant3Vector>(Material);
    Color->Constant=FLinearColor(.075f,.095f,.11f); Material->Expressions.Add(Color); Material->BaseColor.Expression=Color;
    Material->Metallic.Constant=.65f; Material->Roughness.Constant=.42f;
    Material->PostEditChange();
    if (Material->GetBlendMode()!=BLEND_Opaque || Material->bDisableDepthTest) return false;
    UE_LOG(LogTemp,Display,TEXT("Ghost Dominator material self-test: opaque blend, depth testing enabled"));
    return SaveAsset(Package,Material);
}
}

UAHGhostNavigationBuildCommandlet::UAHGhostNavigationBuildCommandlet()
{
    IsClient=true; // Layout preview needs client textures and the Slate renderer.
    IsEditor=true;
    LogToConsole=true;
}

int32 UAHGhostNavigationBuildCommandlet::Main(const FString&)
{
    if (!FSlateApplication::IsInitialized())
        FSlateApplication::InitializeAsStandaloneApplication(
            FModuleManager::LoadModuleChecked<ISlateRHIRendererModule>(TEXT("SlateRHIRenderer")).CreateSlateRHIRenderer());
    UPackage* Package=CreatePackage(TEXT("/Game/Mods/AHGhostCoopNavigation/WBP_GhostTag"));
    UWidgetBlueprint* Widget=CastChecked<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UUserWidget::StaticClass(),Package,TEXT("WBP_GhostTag"),BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass()));
    USizeBox* Root=Widget->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("TagRoot"));
    Root->SetWidthOverride(300); Root->SetHeightOverride(90);
    Widget->WidgetTree->RootWidget=Root;
    UVerticalBox* Rows=Widget->WidgetTree->ConstructWidget<UVerticalBox>();
    Root->AddChild(Rows);
    UObject* Font=LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    if (!Font) return 1;
    for (const FName Name : {FName(TEXT("NameText")),FName(TEXT("DistanceText"))})
    {
        UTextBlock* Text=Widget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
        Text->bIsVariable=true;
        Text->SetFont(FSlateFontInfo(Font,Name==TEXT("NameText") ? 22 : 16,TEXT("Bold")));
        Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.25f,0.95f,0.95f,1)));
        Text->SetShadowColorAndOpacity(FLinearColor::Black); Text->SetShadowOffset(FVector2D(1,2));
        Text->SetJustification(ETextJustify::Center); Text->SetAutoWrapText(true);
        Text->SetText(FText::FromString(Name==TEXT("NameText") ? TEXT("Friend") : TEXT("0 m")));
        Rows->AddChildToVerticalBox(Text)->SetHorizontalAlignment(HAlign_Fill);
    }
    FKismetEditorUtilities::CompileBlueprint(Widget);
    if (Widget->Status==BS_Error) return 2;
    FAssetRegistryModule::AssetCreated(Widget);
    Package->MarkPackageDirty();
    const FString File=FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
    if (!UPackage::SavePackage(Package,Widget,RF_Public|RF_Standalone,*File,GError,nullptr,false,true,SAVE_NoError)) return 3;
    return BuildMenu() && BuildOpaqueWeapon() ? 0 : 4;
}
