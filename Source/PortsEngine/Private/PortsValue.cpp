#include "PortsValue.h"

namespace
{
	const FPortsValue UndefinedValue;
	const FString EmptyString;

	void AppendString(FString& Out, const FString& Text)
	{
		Out.AppendChar(TEXT('"'));
		for (const TCHAR Ch : Text)
		{
			switch (Ch)
			{
			case TEXT('"'): Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\b'): Out += TEXT("\\b"); break;
			case TEXT('\f'): Out += TEXT("\\f"); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\r'): Out += TEXT("\\r"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			default:
				if (Ch < 0x20) Out += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(Ch));
				else Out.AppendChar(Ch);
			}
		}
		Out.AppendChar(TEXT('"'));
	}

	struct FParser
	{
		const TCHAR* Ch;

		void SkipSpace() { while (*Ch == TEXT(' ') || *Ch == TEXT('\n') || *Ch == TEXT('\r') || *Ch == TEXT('\t')) ++Ch; }

		bool Literal(const TCHAR* Word)
		{
			const int32 Len = FCString::Strlen(Word);
			if (FCString::Strncmp(Ch, Word, Len) != 0) return false;
			Ch += Len;
			return true;
		}

		bool String(FString& Out)
		{
			if (*Ch != TEXT('"')) return false;
			++Ch;
			Out.Reset();
			while (*Ch && *Ch != TEXT('"'))
			{
				if (*Ch != TEXT('\\')) { Out.AppendChar(*Ch++); continue; }
				++Ch;
				switch (*Ch)
				{
				case TEXT('"'): Out.AppendChar(TEXT('"')); break;
				case TEXT('\\'): Out.AppendChar(TEXT('\\')); break;
				case TEXT('/'): Out.AppendChar(TEXT('/')); break;
				case TEXT('b'): Out.AppendChar(TEXT('\b')); break;
				case TEXT('f'): Out.AppendChar(TEXT('\f')); break;
				case TEXT('n'): Out.AppendChar(TEXT('\n')); break;
				case TEXT('r'): Out.AppendChar(TEXT('\r')); break;
				case TEXT('t'): Out.AppendChar(TEXT('\t')); break;
				case TEXT('u'):
				{
					uint32 Code = 0;
					for (int32 i = 1; i <= 4; i++)
					{
						if (!FChar::IsHexDigit(Ch[i])) return false;
						Code = Code * 16 + FParse::HexDigit(Ch[i]);
					}
					Out.AppendChar(static_cast<TCHAR>(Code));
					Ch += 4;
					break;
				}
				default: return false;
				}
				++Ch;
			}
			if (*Ch != TEXT('"')) return false;
			++Ch;
			return true;
		}

		bool Value(FPortsValue& Out)
		{
			SkipSpace();
			if (*Ch == TEXT('{'))
			{
				++Ch;
				Out = FPortsValue::Object();
				SkipSpace();
				if (*Ch == TEXT('}')) { ++Ch; return true; }
				for (;;)
				{
					SkipSpace();
					FString Key;
					if (!String(Key)) return false;
					SkipSpace();
					if (*Ch != TEXT(':')) return false;
					++Ch;
					FPortsValue Item;
					if (!Value(Item)) return false;
					Out.Set(*Key, Item);
					SkipSpace();
					if (*Ch == TEXT(',')) { ++Ch; continue; }
					if (*Ch == TEXT('}')) { ++Ch; return true; }
					return false;
				}
			}
			if (*Ch == TEXT('['))
			{
				++Ch;
				Out = FPortsValue::Array();
				SkipSpace();
				if (*Ch == TEXT(']')) { ++Ch; return true; }
				for (;;)
				{
					FPortsValue Item;
					if (!Value(Item)) return false;
					Out.Add(Item);
					SkipSpace();
					if (*Ch == TEXT(',')) { ++Ch; continue; }
					if (*Ch == TEXT(']')) { ++Ch; return true; }
					return false;
				}
			}
			if (*Ch == TEXT('"'))
			{
				FString Text;
				if (!String(Text)) return false;
				Out = FPortsValue(Text);
				return true;
			}
			if (Literal(TEXT("true"))) { Out = FPortsValue(true); return true; }
			if (Literal(TEXT("false"))) { Out = FPortsValue(false); return true; }
			if (Literal(TEXT("null"))) { Out = FPortsValue::Null(); return true; }
			const TCHAR* Start = Ch;
			while (*Ch == TEXT('-') || *Ch == TEXT('+') || *Ch == TEXT('.') || *Ch == TEXT('e') || *Ch == TEXT('E') || (*Ch >= TEXT('0') && *Ch <= TEXT('9'))) ++Ch;
			if (Ch == Start) return false;
			Out = FPortsValue(FCString::Atod(*FString::ConstructFromPtrSize(Start, UE_PTRDIFF_TO_INT32(Ch - Start))));
			return true;
		}
	};
}

FPortsValue FPortsValue::Array(std::initializer_list<FPortsValue> InItems)
{
	FPortsValue V = Array();
	for (const FPortsValue& Item : InItems) V.Items.Add(Item);
	return V;
}

FPortsValue FPortsValue::Object(std::initializer_list<FPortsEntry> Entries)
{
	FPortsValue V = Object();
	for (const FPortsEntry& Entry : Entries) V.Set(Entry.Key, Entry.Value);
	return V;
}

FPortsValue FPortsValue::Strings(const TArray<FString>& InItems)
{
	FPortsValue V = Array();
	for (const FString& Item : InItems) V.Items.Add(FPortsValue(Item));
	return V;
}

FPortsValue FPortsValue::Ints(const TArray<int32>& InItems)
{
	FPortsValue V = Array();
	for (const int32 Item : InItems) V.Items.Add(FPortsValue(Item));
	return V;
}

bool FPortsValue::Truthy() const
{
	switch (Type)
	{
	case EType::Bool: return Flag;
	case EType::Number: return Number != 0 && !FMath::IsNaN(Number);
	case EType::String: return !Text.IsEmpty();
	case EType::Array:
	case EType::Object: return true;
	default: return false;
	}
}

const FString& FPortsValue::AsString() const
{
	return Type == EType::String ? Text : EmptyString;
}

const FPortsValue& FPortsValue::operator[](int32 Index) const
{
	return Type == EType::Array && Items.IsValidIndex(Index) ? Items[Index] : UndefinedValue;
}

FPortsValue& FPortsValue::Add(const FPortsValue& Item)
{
	return Items.Add_GetRef(Item);
}

TArray<FString> FPortsValue::ToStrings() const
{
	TArray<FString> Out;
	if (Type == EType::Array) for (const FPortsValue& Item : Items) Out.Add(Item.AsString());
	return Out;
}

TArray<int32> FPortsValue::ToInts() const
{
	TArray<int32> Out;
	if (Type == EType::Array) for (const FPortsValue& Item : Items) Out.Add(Item.AsInt());
	return Out;
}

bool FPortsValue::ContainsString(const FString& Value) const
{
	if (Type != EType::Array) return false;
	for (const FPortsValue& Item : Items) if (Item.Type == EType::String && Item.Text == Value) return true;
	return false;
}

bool FPortsValue::Has(const TCHAR* Key) const
{
	return Type == EType::Object && Keys.IndexOfByPredicate([Key](const FString& K) { return K.Equals(Key, ESearchCase::CaseSensitive); }) != INDEX_NONE;
}

const FPortsValue& FPortsValue::Get(const TCHAR* Key) const
{
	if (Type != EType::Object) return UndefinedValue;
	const int32 Index = Keys.IndexOfByPredicate([Key](const FString& K) { return K.Equals(Key, ESearchCase::CaseSensitive); });
	return Index == INDEX_NONE ? UndefinedValue : Items[Index];
}

FPortsValue* FPortsValue::Find(const TCHAR* Key)
{
	if (Type != EType::Object) return nullptr;
	const int32 Index = Keys.IndexOfByPredicate([Key](const FString& K) { return K.Equals(Key, ESearchCase::CaseSensitive); });
	return Index == INDEX_NONE ? nullptr : &Items[Index];
}

FPortsValue& FPortsValue::Set(const TCHAR* Key, const FPortsValue& Value)
{
	if (FPortsValue* Existing = Find(Key))
	{
		*Existing = Value;
		return *Existing;
	}
	Keys.Add(Key);
	return Items.Add_GetRef(Value);
}

void FPortsValue::Remove(const TCHAR* Key)
{
	const int32 Index = Keys.IndexOfByPredicate([Key](const FString& K) { return K.Equals(Key, ESearchCase::CaseSensitive); });
	if (Index != INDEX_NONE)
	{
		Keys.RemoveAt(Index);
		Items.RemoveAt(Index);
	}
}

void FPortsValue::AppendJson(FString& Out) const
{
	switch (Type)
	{
	case EType::Undefined:
	case EType::Null:
		Out += TEXT("null");
		break;
	case EType::Bool:
		Out += Flag ? TEXT("true") : TEXT("false");
		break;
	case EType::Number:
		if (!FMath::IsFinite(Number)) Out += TEXT("null");
		else if (Number == FMath::FloorToDouble(Number) && FMath::Abs(Number) < 1e15) Out += FString::Printf(TEXT("%lld"), static_cast<int64>(Number));
		else Out += FString::Printf(TEXT("%.17g"), Number);
		break;
	case EType::String:
		AppendString(Out, Text);
		break;
	case EType::Array:
		Out.AppendChar(TEXT('['));
		for (int32 i = 0; i < Items.Num(); i++)
		{
			if (i) Out.AppendChar(TEXT(','));
			Items[i].AppendJson(Out);
		}
		Out.AppendChar(TEXT(']'));
		break;
	case EType::Object:
	{
		Out.AppendChar(TEXT('{'));
		bool bFirst = true;
		for (int32 i = 0; i < Keys.Num(); i++)
		{
			// JSON.stringify leaves out keys whose value is undefined.
			if (Items[i].Type == EType::Undefined) continue;
			if (!bFirst) Out.AppendChar(TEXT(','));
			bFirst = false;
			AppendString(Out, Keys[i]);
			Out.AppendChar(TEXT(':'));
			Items[i].AppendJson(Out);
		}
		Out.AppendChar(TEXT('}'));
		break;
	}
	}
}

FString FPortsValue::ToJson() const
{
	FString Out;
	AppendJson(Out);
	return Out;
}

bool FPortsValue::Parse(const FString& Json, FPortsValue& Out)
{
	FParser Parser{ *Json };
	if (!Parser.Value(Out)) return false;
	Parser.SkipSpace();
	return *Parser.Ch == 0;
}

bool FPortsValue::operator==(const FPortsValue& Other) const
{
	if (Type != Other.Type) return false;
	switch (Type)
	{
	case EType::Bool: return Flag == Other.Flag;
	case EType::Number: return Number == Other.Number;
	case EType::String: return Text.Equals(Other.Text, ESearchCase::CaseSensitive);
	case EType::Array: return Items == Other.Items;
	case EType::Object: return Keys == Other.Keys && Items == Other.Items;
	default: return true;
	}
}
