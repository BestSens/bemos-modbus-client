/*
 * rip_exception.hpp
 *
 *  Created on: 10.08.2018
 *	  Author: Jan Schöppach
 */

#ifndef RIO_EXCEPTION_HPP
#define RIO_EXCEPTION_HPP

#include <stdexcept>
#include <sstream>
#include <iomanip>

class rio_exception : public std::runtime_error {
	public:
		rio_exception(uint16_t address_, int16_t errorcode_) : std::runtime_error("rio error") {
			this->address = address_;
			this->errorcode = errorcode_;

			std::ostringstream stream;
			stream << getMessageFromCode(this->getErrorCode()) << " @ 0x" << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << this->getAddress();
			this->message = stream.str();
		}

		virtual const char* what() const throw() {
			return message.c_str();
		}

		uint16_t getAddress() const {
			return this->address;
		}

		int16_t getErrorCode() const {
			return this->errorcode;
		}

		static std::string getMessageFromCode(uint16_t errorcode) {
			if(errorcode == 0x7FFF || errorcode == 0x8001) return "short circuit";
			if(errorcode == 0x7FFA || errorcode == 0x8006) return "open circuit";
			if(errorcode == 0x7FF9) return "upper boundary reached";
			if(errorcode == 0x8008) return "lower boundary reached";
			if(errorcode == 0x8010) return "compare value error";
			if(errorcode == 0x8013) return "two conductor matching error";
			if(errorcode == 0x8020) return "no report from IO module";
			if(errorcode == 0x8021) return "IO module configuration error";
			if(errorcode == 0x8022) return "no data from IO module";
			if(errorcode == 0x8023) return "IO module hardware error";
			
			return "undefined error";
		}
	private:
		std::string message;
		uint16_t address;
		int16_t errorcode;
};

#endif /* RIO_EXCEPTION_HPP */
