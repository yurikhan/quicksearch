#ifndef WIN32_EXCEPTION_H
#define WIN32_EXCEPTION_H

#include <windows.h>
#include <stdexcept>
#include <sstream>
#include <ostream>
#include <iomanip>

namespace win32
{

class exception : public std::runtime_error
{
private:
	DWORD errorCode_;
	static std::string error_message(DWORD errorCode);
public:
	explicit exception(DWORD errorCode);
	exception(const exception& other);
	void swap(exception& other);
	exception& operator=(const exception& other);
	DWORD errorCode() const;
};

template <typename T> T check(T result, T errorValue = T(0));

inline HANDLE check_handle(HANDLE result, HANDLE errorValue = INVALID_HANDLE_VALUE);

// implementation

inline /*static*/ std::string
exception::error_message(DWORD errorCode)
{
	std::ostringstream oss;
	oss << "Win32 error 0x" << std::hex << std::setw(8) << std::setfill('0') << errorCode;

	LPVOID message = 0;

	if (FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		FORMAT_MESSAGE_FROM_SYSTEM | 
		FORMAT_MESSAGE_IGNORE_INSERTS,
		/*lpSource*/0,
		errorCode,
		MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), // RATIONALE: localized error texts are ungooglable
		reinterpret_cast<LPSTR>(&message),
		/*nSize*/0,
		/*Arguments*/0))
	{
		oss << ": " << reinterpret_cast<const char*>(message);
		LocalFree(message);
	}

	return oss.str();
}

inline /*explicit*/
exception::exception(DWORD errorCode)
	: std::runtime_error(error_message(errorCode)), errorCode_(errorCode)
{}

inline
exception::exception(const exception& other)
	: std::runtime_error(other), errorCode_(other.errorCode_)
{}

inline void
exception::swap(exception& other)
{
	using std::swap;
	swap(static_cast<std::runtime_error&>(*this), static_cast<std::runtime_error&>(other));
	swap(errorCode_, other.errorCode_);
}

inline exception&
exception::operator=(const exception& other)
{
	exception copy(other);
	copy.swap(*this);
	return *this;
}

inline DWORD
exception::errorCode() const
{
	return errorCode_;
}

template <typename T>
T check(T result, T errorValue)
{
	if (errorValue == result)
	{
		throw exception(::GetLastError());
	}
	return result;
}

inline HANDLE
check_handle(HANDLE result, HANDLE errorValue)
{
	return check<HANDLE>(result, errorValue);
}

}

#endif
