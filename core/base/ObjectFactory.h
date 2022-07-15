/****************************************************************************
Copyright (c) 2013-2016 Chukong Technologies Inc.
Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.

https://axis-project.github.io/

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/

#ifndef __TRIGGERFACTORY_H__
#define __TRIGGERFACTORY_H__

#include <string>
#include <unordered_map>
#include <functional>
#include "base/CCRef.h"
#include "platform/CCPlatformMacros.h"

NS_AX_BEGIN

class AX_DLL ObjectFactory
{
public:
    typedef axis::Ref* (*Instance)(void);
    typedef std::function<axis::Ref*(void)> InstanceFunc;
    struct AX_DLL TInfo
    {
        TInfo();
        TInfo(std::string_view type, Instance ins = nullptr);
        TInfo(std::string_view type, InstanceFunc ins = nullptr);
        TInfo(const TInfo& t);
        ~TInfo();
        TInfo& operator=(const TInfo& t);
        std::string _class;
        Instance _fun;
        InstanceFunc _func;
    };
    typedef hlookup::string_map<TInfo> FactoryMap;

    static ObjectFactory* getInstance();
    static void destroyInstance();
    axis::Ref* createObject(std::string_view name);

    void registerType(const TInfo& t);
    void removeAll();

protected:
    ObjectFactory();
    virtual ~ObjectFactory();

    static ObjectFactory* _sharedFactory;
    FactoryMap _typeMap;
};

NS_AX_END

#endif
