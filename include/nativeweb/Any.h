#ifndef _Any_INCLUDED
#define _Any_INCLUDED

// Project-owner supplied Any.h.
//
// NativeWeb intentionally keeps the original public Any / Variant / VariantList /
// VariantDict model. Only portability fixes required by current compilers are
// applied here:
//   - include <stdexcept> explicitly for std::runtime_error
//   - restrict the legacy global type_info compatibility typedef to old MSVC
//   - add ergonomic key/index overloads so obj["key"] and list[0] are unambiguous

#include <memory>//std::shared_ptr
#include <iostream>
#include <algorithm>
#include <typeinfo>
#include <vector>
#include <map>
#include <string>
#include <stdexcept>

class Any;

typedef std::map< std::string, Any > VariantDict;
typedef std::vector< Any > VariantList;
typedef Any Variant;

#if defined(_MSC_VER) && _MSC_VER < 1300
namespace std {
typedef ::type_info type_info;
}
#endif

//namespace Async {

class Any
{
public:
	Any() :_content(0)
	{
	}

	template <typename ValueType>
	Any(const ValueType& value)
	: _content(std::make_shared< Holder<ValueType> >(value))
	{
	}

	Any(const char* value)
	: _content(std::make_shared< Holder<std::string> >(value))
	{
	}

	Any(const Any& other)
	: _content(other._content)
	//_content(other._content ? other._content->clone() : 0)
	{
	}

	~Any()
	{
		//delete _content;
	}

	Any& swap(Any& rhs)
		/// Swaps the content of the two Anys.
	{
		std::swap(_content, rhs._content);
		return *this;
	}

	template <typename ValueType>
	Any& operator = (const ValueType& rhs)
	{
		Any(rhs).swap(*this);
		return *this;
	}

	Any& operator = (const Any& rhs)
		/// Assignment operator for Any.
	{
		Any(rhs).swap(*this);
		return *this;
	}

	bool empty() const
		/// returns true if the Any is empty
	{
		return !_content;
	}

	const std::type_info& type() const
	{
		return _content ? _content->type() : typeid(void);
	}

	operator int() const
	{
		return static_cast<Any::Holder< int >*>(_content.get())->_held;
	}

	operator double() const
	{
		return static_cast<Any::Holder< double >*>(_content.get())->_held;
	}

	operator bool() const
	{
		return static_cast<Any::Holder< bool >*>(_content.get())->_held;
	}

	operator std::string() const
	{
		return static_cast<Any::Holder< std::string >*>(_content.get())->_held;
	}

	operator VariantDict() const
	{
		return static_cast<Any::Holder< VariantDict >*>(_content.get())->_held;
	}

	operator VariantList() const
	{
		return static_cast<Any::Holder< VariantList >*>(_content.get())->_held;
	}

	Any& operator [](size_t idx)
	{
		if(type() != typeid(VariantList))
			throw std::runtime_error("type()==VariantList required : " + std::string(type().name()));

		return (static_cast<Any::Holder< VariantList >*>(_content.get())->_held)[idx];
	}

	const Any& operator [](size_t idx) const
	{
		if(type() != typeid(VariantList))
			throw std::runtime_error("type()==VariantList required : " + std::string(type().name()));

		return (static_cast<Any::Holder< VariantList >*>(_content.get())->_held)[idx];
	}

	Any& operator [](int idx)
	{
		if(idx < 0)
			throw std::out_of_range("VariantList index must be non-negative");

		return (*this)[static_cast<size_t>(idx)];
	}

	const Any& operator [](int idx) const
	{
		if(idx < 0)
			throw std::out_of_range("VariantList index must be non-negative");

		return (*this)[static_cast<size_t>(idx)];
	}

	Any& operator [](const std::string& key)
	{
		if(type() != typeid(VariantDict))
			throw std::runtime_error("type()==VariantDict required : " + std::string(type().name()));

		return (static_cast<Any::Holder< VariantDict >*>(_content.get())->_held)[key];
	}

	const Any& operator [](const std::string& key) const
	{
		if(type() != typeid(VariantDict))
			throw std::runtime_error("type()==VariantDict required : " + std::string(type().name()));

		return (static_cast<Any::Holder< VariantDict >*>(_content.get())->_held)[key];
	}

	Any& operator [](const char* key)
	{
		return (*this)[std::string(key)];
	}

	const Any& operator [](const char* key) const
	{
		return (*this)[std::string(key)];
	}

private:
	class Placeholder
	{
	public:
		virtual ~Placeholder()
		{
		}

		virtual const std::type_info& type() const = 0;
		virtual Placeholder* clone() const = 0;
	};

	template <typename ValueType>
	class Holder: public Placeholder
	{
	public:
		Holder(const ValueType& value):
			_held(value)
		{
		}

		virtual const std::type_info& type() const
		{
			return typeid(ValueType);
		}

		virtual Placeholder* clone() const
		{
			return new Holder(_held);
		}

		ValueType _held;
	};

private:
	template <typename ValueType>
	friend ValueType* AnyCast(Any*);

	//Placeholder* _content;
	std::shared_ptr<Placeholder> _content;
};


template <typename ValueType>
ValueType* AnyCast(Any* operand)
{
	return operand && operand->type() == typeid(ValueType)
				? &static_cast<Any::Holder<ValueType>*>(operand->_content.get())->_held
				: 0;
}


template <typename ValueType>
const ValueType* AnyCast(const Any* operand)
{
	return AnyCast<ValueType>(const_cast<Any*>(operand));
}

template <typename T>
struct TypeWrapper
	/// Use the type wrapper if you want to decouple constness and references from template types.
{
	typedef T TYPE;
	typedef const T CONSTTYPE;
	typedef T& REFTYPE;
	typedef const T& CONSTREFTYPE;
};


template <typename T>
struct TypeWrapper<const T>
{
	typedef T TYPE;
	typedef const T CONSTTYPE;
	typedef T& REFTYPE;
	typedef const T& CONSTREFTYPE;
};


template <typename T>
struct TypeWrapper<const T&>
{
	typedef T TYPE;
	typedef const T CONSTTYPE;
	typedef T& REFTYPE;
	typedef const T& CONSTREFTYPE;
};


template <typename T>
struct TypeWrapper<T&>
{
	typedef T TYPE;
	typedef const T CONSTTYPE;
	typedef T& REFTYPE;
	typedef const T& CONSTREFTYPE;
};


template <typename ValueType>
ValueType AnyCast(Any& operand)
{
	typedef typename TypeWrapper< ValueType >::TYPE NonRef;

	NonRef* result = AnyCast<NonRef>(&operand);
	//if (!result) throw std::exception("Failed to convert between Any types");
	if (!result)
	{
		std::string s = "RefAnyCast: Failed to convert between Any types ";
		if (!operand.empty())
		{
			s.append(1, '(');
			s.append(operand.type().name());
			s.append(" => ");
			s.append(typeid(ValueType).name());
			s.append(1, ')');
		}
		throw std::runtime_error(s);
	}
	return *result;
}


template <typename ValueType>
ValueType AnyCast(const Any& operand)
{
	typedef typename TypeWrapper<ValueType>::TYPE NonRef;

	return AnyCast<NonRef&>(const_cast<Any&>(operand));
}

//} // namespace Async


#endif
