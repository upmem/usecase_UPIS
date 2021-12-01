/*
 * Copyright (c) 2021 - UPMEM
 * This file is part of a project which is released under MIT license.
 * See LICENSE file for details.
 */

#ifndef CMD_PARSER___H_
#define CMD_PARSER___H_

#include <string>
#include <utility>
#include <iostream>
#include <iomanip>
#include <map>

/**
 * Command line parser
 **/
class CommandLineParser{

  public:
    
    CommandLineParser(int &argc, char **argv, const std::pair<std::string, std::string> & cmdDescr,
        const std::map<std::string, std::pair<bool, std::string>> & validOptions) {
    
      _parsingError = false;
      for (int i = 1; i < argc; ++i) {
        std::string str(argv[i]);
        std::string optStr, valStr;
        auto it = str.find('=');
        if(it != std::string::npos) {
          optStr = str.substr(0, it);
          valStr = str.substr(it+1);
        }
        else {
          optStr = str;
        }
        bool valid = false;
        auto itr = validOptions.find(optStr);
        if(itr != validOptions.end()) {
          if(itr->second.first != valStr.empty()) {
            valid = true;
            _options[optStr] = valStr;
          }
        }
        if(!valid && !_parsingError) {
          _parsingError = true;
          _invalidOption = optStr;
        }
      }

      std::ostringstream ossHelp;
      ossHelp << ">> " << cmdDescr.first << ": " << cmdDescr.second << "\n";
      for(auto const & o : validOptions) {
        ossHelp << "\t" << o.first << ": " << o.second.second << "\n";
      }
      _help = ossHelp.str();
    }

    bool IsValid(std::string & invalidOpt) const {

      invalidOpt = _invalidOption;
      return !_parsingError;
    }

    const std::string & GetCmdOption(const std::string & option) const {

      auto itr = _options.find(option);
      if (itr != _options.end()){
        return itr->second;
      }
      static const std::string empty_string("");
      return empty_string;
    }

    bool CmdOptionExists(const std::string &option) const{

      return _options.find(option) != _options.end();
    }

    void PrintHelp() const {

      std::cout << _help;
    }

  private:
    
    std::map<std::string, std::string> _options;
    std::string _invalidOption;
    bool _parsingError;
    std::string _help;
};

#endif
