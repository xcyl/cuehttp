# cuehttp

[![Build Status](https://travis-ci.org/xcyl/cuehttp.svg?branch=master)](https://travis-ci.org/xcyl/cuehttp)
[![Codacy Badge](https://api.codacy.com/project/badge/Grade/a11b810953524ef98b92452a3c611d6f)](https://www.codacy.com/manual/xcyl/cuehttp?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=xcyl/cuehttp&amp;utm_campaign=Badge_Grade)
[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/xcyl/cuehttp.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/xcyl/cuehttp/context:cpp)
[![language](https://img.shields.io/badge/language-C++17-red.svg)](https://en.wikipedia.org/wiki/C++17)
[![GitHub license](https://img.shields.io/badge/license-Apache2.0-blue.svg)](https://raw.githubusercontent.com/xcyl/cuehttp/master/LICENSE)

## 简介

cuehttp是一个使用Modern C++(C++17)编写的跨平台、高性能、易用的HTTP/WebSocket框架。基于中间件模式可以方便、高效、优雅的增加功能。cuehttp基于boost.asio开发，使用[picohttpparser](https://github.com/h2o/picohttpparser)进行HTTP协议解析。内部依赖了[nlohmann/json](https://github.com/nlohmann/json)。

cuehttp内部包含一组中间件函数，注册的中间件会根据中间件的添加顺序执行。在中间件中也可以选择是否进行下一个中间件的执行或改变中间件内的行为执行顺序。

## roadmap

-   multipart
-   错误处理
-   http client

## 使用

cuehttp依赖boost，以及使用最低依赖C++17。cuehttp是header-only的，`#include <cuehttp.hpp>`即可使用。HTTPS需要依赖OpenSSL，并且在编译的时候添加`ENABLE_HTTPS`宏。

### hello cuehttp!

cuehttp是非常的简洁易用的，使用use接口即可添加中间件。

```cpp
#include <cuehttp.hpp>

using namespace cue::http;

int main(int argc, char** argv) {
    cuehttp app;
    app.use([](context& ctx) {
        ctx.type("text/html");
        ctx.body(R"(<h1>Hello cuehttp!</h1>)");
        ctx.status(200);
    });
    app.listen(10000).run();

    return 0;
}
```

### https

cuehttp支持HTTPS，并且支持HTTP和HTTPS同时使用。`注：cue::http::cuehttp默认listen创建的是HTTP的，若要使用HTTPS，需要使用https::create_server创建。`

```cpp
#include <cuehttp.hpp>

using namespace cue::http;

int main(int argc, char** argv) {
    cuehttp app;
    app.use([](context& ctx) {
        ctx.type("text/html");
        ctx.body(R"(<h1>Hello, cuehttp!</h1>)");
        ctx.status(200);
    });

    // both
    auto http_server = http::create_server(app.callback());
    http_server.listen(10000);

    auto https_server = https::create_server(app.callback(), "server.key", "server.crt");
    https_server.listen(443);

    cuehttp::run();

    return 0;
}
```

### WebSocket

cuehttp支持WebSocket，支持ws/wss同时使用。支持wss需要开启HTTPS(见上节)。

```cpp
#include <iostream>
#include <vector>

#include <cuehttp.hpp>

using namespace cue::http;

int main(int argc, char** argv) {
    cuehttp app;
    app.ws().use([](context& ctx) {
        ctx.websocket().on_open([&ctx]() {
            std::cout << "websocket on_open" << std::endl;
            ctx.websocket().send("hello");
        });
        ctx.websocket().on_close([]() {
            std::cout << "websocket on_close" << std::endl;
        });
        ctx.websocket().on_message([&ctx](std::string&& msg) {
            std::cout << "websocket msg: " << msg << std::endl;
            ctx.websocket().send(std::move(msg));
        });
    }));

    auto http_server = http::create_server(app.callback());
    http_server.listen(10000);

    auto https_server = https::create_server(app.callback(), "server.key", "server.crt");
    https_server.listen(443);

    cuehttp::run();

    return 0;
}
```

### 中间件级联

cuehttp中的中间件通过next函数调用控制下游中间件的运行，当next函数返回，则继续执行上游中间件。中间件的注册分为两种：

-   `void(context& ctx)`

    中间件在执行完自身逻辑时自动调用next控制下游中间件执行。

-   `void(context& ctx, std::function<void()> next)`

    中间件需要调用next控制下游中间件执行，若next不调用，则下游所有中间件无法执行。

```cpp
#include <iostream>

#include <cuehttp.hpp>

using namespace cue::http;

int main(int argc, char** argv) {
    cuehttp app;
    app.use([](context& ctx) {
        std::cout << "0" << std::endl;
    });

    app.use([](context& ctx, std::function<void()> next) {
        std::cout << "1-1" << std::endl;
        next();
        std::cout << "1-2" << std::endl;
    });

    app.use([](context& ctx, std::function<void()> next) {
        std::cout << "2" << std::endl;
    });

    app.use([](context& ctx, std::function<void()> next) {
        std::cout << "3" << std::endl;
    });

    app.listen(10000).run();

    return 0;
}

// 0
// 1-1
// 2
// 1-2
```

支持多种中间件执行体（普通函数、类成员函数、operator()、std::function、lambda），支持批量添加，支持链式调用。

```cpp
#include <iostream>
#include <vector>

#include <cuehttp.hpp>

using namespace cue::http;

void f1(context& ctx) {
    std::cout << "f1" << std::endl;
}

void f2(context& ctx, std::function<void()> next) {
    std::cout << "f2" << std::endl;
    next();
}

struct handler1 {
    void handle(context& ctx) {
        std::cout << "handler1::handle" << std::endl;
    }
};

struct handler2 {
    void handle(context& ctx, std::function<void()> next) {
        std::cout << "handler2::handle" << std::endl;
        next();
    }
};

struct operator1 {
    void operator()(context& ctx) {
        std::cout << "operator1" << std::endl;
    }
};

struct operator2 {
    void operator()(context& ctx, std::function<void()> next) {
        std::cout << "operator2" << std::endl;
        next();
    }
};

int main(int argc, char** argv) {
    cuehttp app;
    app.use(f1);
    app.use(f2);

    handler1 hr1;
    app.use(&handler1::handle, &hr1);
    app.use(&handler1::handle);

    handler2 hr2;
    app.use(&handler2::handle, &hr2);
    app.use(&handler2::handle);

    operator1 or1;
    app.use(or1);

    operator2 or2;
    app.use(or2);

    app.use([](context& ctx) {
           ctx.type("text/html");
           ctx.body(R"(<h1>Hello, cuehttp!</h1>)");
           ctx.status(200);
       })
        .use([](context& ctx, std::function<void()> next) {
            std::cout << "1-1" << std::endl;
            next();
            std::cout << "1-2" << std::endl;
        });

    const std::vector<std::function<void(context&, std::function<void()>)>> handlers{
        [](context& ctx, std::function<void()> next) {
            std::cout << "2-1" << std::endl;
            next();
            std::cout << "2-2" << std::endl;
        },
        [](context& ctx, std::function<void()> next) {
            std::cout << "3-1" << std::endl;
            std::cout << "3-2" << std::endl;
            next();
        }};
    app.use(std::move(handlers));

    app.use([](context& ctx) { std::cout << "4" << std::endl; });

    app.listen(10000).run();
    // or
    // app.listen(10000);
    // cuehttp::run();

    // or
    // http::create_server(app.callback()).listen(10000).run();

    // or
    // auto http_server = http::create_server(app.callback());
    // http_server.listen(10000);
    // cuehttp::run();

    return 0;
}
```

### chunked

支持chunked响应。

```cpp
#include <cuehttp.hpp>

using namespace cue::http;

int main(int argc, char** argv) {
    router route;
    route.get("/chunked", [](context& ctx) {
        ctx.status(200);
        ctx.set("Transfer-Encoding", "chunked");
        ctx.body() << R"(<h1>Hello, cuehttp!</h1>)";
    });

    cuehttp app;
    app.use(route.routes());

    app.listen(10000).run();

    return 0;
}
```

## API

>   类型说明:
>
>   `string`: 所有std::string值类型、引用类型或可以转换为std::string的类型；
>
>   `map`: std::map的所有值类型或引用类型。

### cue::http::cuehttp

cuehttp主体程序，用于注册中间件、启停HTTP服务。

#### cue::http::cuehttp& use(...)

注册中间件到cuehttp中，返回cuehttp对象的引用用于进行链式调用。具体使用参考[中间件级联](#中间件级联)，[内置中间件](#内置中间件)。

#### cue::http::cuehttp& listen(unsigned port, [string host])

监听端口，此接口为阻塞接口，host为可选的。

#### std::function<void(context&)> callback() const

返回设置给server的handler。

#### static void run()

运行服务。

#### static void stop()

停止服务。

#### ws_server& ws()

返回WebSocket server操作示例。

### cue::http::server

#### cue::http::server& listen(unsigned port, [string host])

监听端口，此接口为阻塞接口，host为可选的。

#### void run()

运行服务。

### cue::http::http

#### cue::http::server create_server(std::function<void(context&)> handler)

创建HTTP服务，传入handler。

```cpp
using namespace cue::http;
cuehttp app;
...
auto server = http::create_server(app.callback());
server.listen(10000).run();
```

### cue::http::https

#### cue::http::server create_server(std::function<void(context&)> handler, const std::string& key, const std::string& cert)

创建HTTPS服务，传入handler。

```cpp
using namespace cue::http;
cuehttp app;
...
auto server = https::create_server(app.callback(), "server.key", "server.crt");
server.listen(10000).run();
```

### cue::http::ws_server

#### cue::http::ws_server& use(...)

注册中间件到WebSocket server中，返回ws_server对象的引用用于进行链式调用。具体使用参考[中间件级联](#中间件级联)，[内置中间件](#内置中间件)。

#### void broadcast(std::string_view msg, [ws_send::options options])

对所有连接的客户端进行消息广播。
| ws_send::options | 类型 | 描述                       | 默认值 |
| ---------------- | ---- | -------------------------- | ------ |
| fin              | bool | 发送的消息是否为最后结束帧 | true   |
| mask             | bool | payload是否需要掩码加密    | true   |
| binary           | bool | 可以访问此cookie的域名     | false  |

#### std::function<void(context&)> callback()

返回设置给WebSocket处理中间件handler。

### cue::http::context

chehttp中间件接口的HTTP处理上下文。

#### cue::http::request& req()

获取会话中的HTTP请求结构的引用。

#### cue::http::response& res()

获取会话中的响应结构的引用。

#### cue::http::websocket& websocket()

获取会话中的websocket结构的引用。

#### const std::map<std::string, std::string>& headers() const

获取请求携带的header信息，header的field以及value键值对组。

#### std::string_view get(std::string_view field) const

获取请求中header信息中对应field的value，无则返回空字符串。

#### std::string_view method() const

获取请求的method。

#### std::string_view host() const

获取请求中host。hostname:port。

```cpp
127.0.0.1:1000
```

#### std::string_view hostname() const

获取请求中的hostname。

```cpp
127.0.0.1
```

#### std::string_view url() const

获取请求的url。

```cpp
/get?sid=hosdghtsdvojoj
```

#### std::string_view origin() const

获取请求的url来源，包含protocol和host。

```cpp
http://127.0.0.1:10000
```

#### std::string_view href() const

获取请求的完整url，包含protocol、host、url。

```cpp
http://127.0.0.1:10000/get?sid=hosdghtsdvojoj
```

#### std::string_view path() const

获取请求路径名。

```cpp
/get
```

#### std::string_view querystring() const

获取请求查询字符串。

```cpp
sid=hosdghtsdvojoj
```

#### unsigned status() const

获取响应的status。

#### void status(unsigned status)

设置响应的status。

#### void redirect(string url)

重定向到对应url，默认status为302，若修改默认的302则在redirect前调用status。

#### void message(string message)

设置响应的信息，对应status。

#### void set(string field, string value)

向响应中添加header。

#### void set(map<std::string, std::string> headers)

向响应中添加一组header。

#### void remove(std::string_view field)

将响应中对应field的header删除。

#### void type(string content_type)

设置响应体的类型。

#### void length(std::uint64_t content_length)

设置响应体的长度。若为chunked类型响应，则此项不设置。

#### cookies& cookies()

获取cookies操作对象。

#### bool has_body() const

响应中是否已经设置body。

#### void body(string body)

设置响应体。

#### void body(const char* buffer, std::size_t size)

设置响应体，传入buffer和buffer大小。

#### std::ostream& body()

获取响应体流操作对象(`std::ostream`)，用于以流的方式设置响应体，调用接口时发送响应header，流操作时发送响应体。

```cpp
ctx.body() << "hello cuehttp";
```

### cue::http::request

#### std::string_view get(std::string_view field) const

获取请求中header信息中对应field的value，无则返回空字符串。

#### const std::map<std::string, std::string>& headers() const

获取请求携带的header信息，header的field以及value键值对组。

#### std::string_view method() const

获取请求的method。

#### std::string_view host() const

获取请求中host。hostname:port。

```cpp
127.0.0.1:1000
```

#### std::string_view hostname() const

获取请求中的hostname。

```cpp
127.0.0.1
```

#### std::string_view url() const

获取请求的url。

```cpp
/get?sid=hosdghtsdvojoj
```

#### std::string_view origin() const

获取请求的url来源，包含protocol和host。

```cpp
http://127.0.0.1:10000
```

#### std::string_view href() const

获取请求的完整url，包含protocol、host、url。

```cpp
http://127.0.0.1:10000/get?sid=hosdghtsdvojoj
```

#### std::string_view path() const

获取请求路径名。

```cpp
/get
```

#### std::string_view querystring() const

获取请求查询字符串。

```cpp
sid=hosdghtsdvojoj
```

#### const std::map<std::string, std::string>& query() const

获取请求的query列表。name和value键值对组。

#### std::string_view search() const

获取请求查询字符串，带?。

```cpp
?sid=hosdghtsdvojoj
```

#### std::string_view type() const

获取请求的content type。

#### std::string_view charset() const

获取请求content的charset。

#### std::uint64_t length() const

获取请求的content length。

#### std::string_view body()

获取请求携带的body。

### cue::http::response

#### unsigned status() const

获取响应的status。

#### void status(unsigned status)

设置响应的status。

#### void message(string message)

设置响应的信息，对应status。

#### bool has(std::string_view field) const

获取响应是否设置了某个header。

#### std::string_view get(std::string_view field) const

获取响应中某个header，不存在返回空。

#### void set(string field, string value)

向响应中添加header。

#### void set(map<std::string, std::string> headers)

向响应中添加一组header。

#### void remove(std::string_view field)

将响应中对应field的header删除。

#### void redirect(string url)

重定向到对应url，默认status为302，若修改默认的302则在redirect前调用status。

#### void type(string content_type)

设置响应体的类型。

#### void length(std::uint64_t content_length)

设置响应体的长度。若为chunked类型响应，则此项不设置。

#### bool has_body() const

响应中是否已经设置body。

#### void body(string body)

设置响应体，`std::string`类型。

#### void body(const char* buffer, std::size_t size)

设置响应体，传入buffer和buffer大小。

#### std::ostream& body()

获取响应体流操作对象(`std::ostream`)，用于以流的方式设置响应体，调用接口时发送响应header，流操作时发送响应体。

```cpp
response.body() << "hello cuehttp";
```

### cue::http::websocket

#### void on_open(std::function<void()> func)

设置WebSocket连接建立回调。

#### void on_close(std::function<void()> func)

设置WebSocket连接关闭回调。

#### void on_message(std::function\<void(std::string&&)> func)

设置WebSocket连接消息回调。

#### void send(string msg, [ws_send::options options])

向客户端发送消息。options同[options](#void broadcast(std::string_view msg, [ws_send::options options]))。

#### void close()

向客户端发送关闭WebSocket连接消息。

### cue::http::cookies

#### std::string_view get(std::string_view name) const

获取请求中携带的cookie的值，无则返回空。

#### void set(string name, string value, [cookie::options options])

添加响应的Set-Cookie头，每调用一次添加一个。name/value为空则cookie无效。

| cookie::options | 类型        | 描述                                                      | 默认值 |
| --------------- | ----------- | --------------------------------------------------------- | ------ |
| max_age         | int         | cookie过期时间(单位秒)，-1则不设置max-age                 | -1     |
| expires         | std::string | cookie过期日期(GMT格式)，与max-age同时设置时以max-age为准 |        |
| path            | std::string | 可以访问此cookie的页面路径                                |        |
| domain          | std::string | 可以访问此cookie的域名                                    |        |
| secure          | bool        | 设置是否只能通过https来传递此cookie                       | false  |
| http_only       | bool        | 是否只能通过http访问此cookie                              | false  |

#### const std::vector\<cue::http::cookie>& get() const

获取设置给响应的所有cookie。

### cue::http::cookie

#### std::string_view get(std::string_view name) const

获取cookie的值，无则返回空。

#### void set(string name, string value, [cookie::options options])

设置cookie。options同[options](#void set(string name, string value, [cookie::options options]))。

#### std::string_view name() const

获取cookie的键，无则返回空。

#### void name(string name)

设置cookie的键。

#### std::string_view value() const

获取cookie的值，无则返回空。

#### void value(string value)

设置cookie的值。

#### int max_age() const

获取cookie过期时间，无则返回-1。

#### void max_age(int max_age)

设置cookie过期时间。

#### std::string_view expires() const

获取cookie过期日期，无则返回空。

#### void expires(string date)

设置cookie过期日期。

#### std::string_view path() const

获取cookie允许的路径，默认返回"/"。

#### void path(string path)

设置cookie允许的路径。

#### std::string_view domain() const

获取cookie允许的域名。

#### void domain(string domain)

设置cookie允许的域名。

#### bool secure() const

获取是否只能通过https来传递此cookie。

#### void secure(bool secure)

设置是否只能通过https来传递此cookie。

#### bool http_only() const

获取是否只能通过http访问此cookie。

#### void http_only(bool http_only)

设置是否只能通过http访问此cookie。

## 内置中间件

### router

默认的cuehttp的server是不包含HTTP/WebSocket路由功能的，所有的HTTP请求都将回调注册的中间件，router是cuehttp默认的路由中间件，router会根据注册的HTTP method和path进行请求分发。router支持配置多种method、multiple、redirect等。router也可用于WebSocket server。

#### 示例

```cpp
#include <cuehttp.hpp>

using namespace cue::http;

int main(int argc, char** argv) {
    router route;
    route.get("/get", [](context& ctx) {
        ctx.type("text/html");
        ctx.body(R"(<h1>Hello, cuehttp!</h1>)");
    });

    cuehttp app;
    app.use(route.routes());

    router ws_route;
    app.ws().use(ws_route.all("/ws", [](context& ctx) {
        ctx.websocket().on_open([&ctx]() {
            std::cout << "websocket on_open" << std::endl;
            ctx.websocket().send("hello");
        });
        ctx.websocket().on_close([]() { std::cout << "websocket on_close" << std::endl; });
        ctx.websocket().on_message([&ctx](std::string&& msg) {
            std::cout << "websocket msg: " << msg << std::endl;
            ctx.websocket().send(std::move(msg));
        });
    }));

    app.listen(10000).run();

    return 0;
}
```

#### API

##### router([string prefix])

创建router，可选prefix，默认prefix为空。

##### router& prefix(string prefix)

设置prefix。

##### std::function<void(context&)> routes() const

生成router中间件注册函数。

示例：

```cpp
cue::http::cuehttp app;
cue::http::router route;
app.use(route.routes());
```

##### router& del|get|head|post|put|all(std::string_view path, ...)

注册对应method和path的处理函数，支持普通函数、类成员函数、operator()、std::function、lambda、multiple。

`注：multiple中若参数包含next函数，需手动调用next函数，否则无法执行下游处理函数，最后一个不影响。无next参数的自动调用。`

all接口将注册所有支持的method。

示例：

```cpp
void f1(context& ctx) {
    std::cout << "f1" << std::endl;
}

void f2(context& ctx, std::function<void()> next) {
    std::cout << "f2" << std::endl;
    next();
}

struct handler1 {
    void handle(context& ctx) {
        std::cout << "handler1::handle" << std::endl;
    }
};

struct handler2 {
    void handle(context& ctx, std::function<void()> next) {
        std::cout << "handler2::handle" << std::endl;
        next();
    }
};

struct operator1 {
    void operator()(context& ctx) {
        std::cout << "operator1" << std::endl;
    }
};

struct operator2 {
    void operator()(context& ctx, std::function<void()> next) {
        std::cout << "operator2" << std::endl;
        next();
    }
};

route.get("/get1", [](context& ctx) {
    ctx.type("text/html");
    ctx.body(R"(<h1>Hello, cuehttp!</h1>)");
});

route.post("/post", [](context& ctx, std::function<void()> next) {
    std::cout << "handle post: " << ctx.path() << std::endl;
    ctx.type("text/html");
    ctx.body(R"(<h1>Hello, cuehttp!</h1>)");
});

route.get("/get2", f1);
route.get("/get3", f2);

handler1 h1;
route.get("/get4", &handler1::handle, &h1);
route.get("/get5", &handler1::handle);

handler2 h2;
route.get("/get6", &handler2::handle, &h2);
route.get("/get7", &handler2::handle);

operator1 o1;
route.get("/get8", o1);

operator1 o2;
route.get("/get9", o2);

// multiple
route.get(
    "/get_multiple1",
    [](context& ctx, std::function<void()> next) {
        std::cout << "befor get" << std::endl;
        next();
    },
    [](context& ctx, std::function<void()> next) {
        std::cout << "handle get: " << ctx.path() << std::endl;
        ctx.type("text/html");
        ctx.body(R"(<h1>Hello, cuehttp!</h1>)");
        next();
    },
    [](context& ctx, std::function<void()> next) {
        std::cout << "after get" << std::endl;
    });

route.get(
    "/get_multiple2",
    [](context& ctx, std::function<void()> next) {
        std::cout << "befor get" << std::endl;
        next();
        std::cout << "after get" << std::endl;
    },
    [](context& ctx) {
        std::cout << "handle get: " << ctx.path() << std::endl;
        ctx.type("text/html");
        ctx.body(R"(<h1>Hello, cuehttp!</h1>)");
    });

handler1 hr1;
handler2 hr2;

route.get("/get_multiple3", &handler2::handle, &hr2, &handler2::handle,
          [](context& ctx) { std::cout << "after get" << std::endl; });

route.get("/get_multiple4", &handler2::handle, &handler1::handle, &hr1,
          [](context& ctx) { std::cout << "after get" << std::endl; });

route.get(
    "/get_multiple5", [](context& ctx) { std::cout << "befor get" << std::endl; },
    [](context& ctx) {
        std::cout << "handle get: " << ctx.path() << std::endl;
        ctx.type("text/html");
        ctx.body(R"(<h1>Hello, cuehttp!</h1>)");
    },
    [](context& ctx) { std::cout << "after get" << std::endl; });
```

##### router& redirect(std::string_view path, string destination, [unsigned status])

重定向接口，将path重定向到destination。默认status为301。

相当于：

```cpp
route.all(path, [](context& ctx) {
    ctx.redirect(destination);
    ctx.status(301);
});
```

### session

cuehttp提供了简单的session中间件。默认使用cookie进行session交互与管理。支持配置外部存储来管理session，如redis、DB等。

#### 示例

每访问一次/test_session，页面的数字就加一。

```cpp
#include <iostream>

#include <cuehttp.hpp>

using namespace cue::http;

int main(int argc, char** argv) {
    router route;
    route.get("/test_session", [](context& ctx) {
        int view{1};
        const auto view_str = ctx.session().get("view");
        if (view_str.empty()) {
            ctx.session().set("view", std::to_string(view));
        } else {
            view = std::stoi(view_str);
            ctx.session().set("view", std::to_string(view + 1));
        }
        ctx.type("text/html");
        ctx.body(R"(<h1>Hello, )" + std::to_string(view) + R"( cuehttp!</h1>)");
        ctx.status(200);
    });

    session::options session_opt;
    session_opt.key = "cuehttp";
    // session_opt.external_key.get = [](context& ctx) {
    //     std::cout << "external_key.get" << std::endl;
    //     return ctx.get("User-Token");
    // };
    // session_opt.external_key.set = [](context& ctx, std::string_view value) {
    //     std::cout << "external_key.set" << std::endl;
    //     return ctx.set("User-Token", value);
    // };
    // session_opt.external_key.destroy = [](context& ctx, std::string_view value) {
    //     std::cout << "external_key.destroy" << std::endl;
    //     return ctx.remove("User-Token");
    // };
    cuehttp app;
    app.use(use_session(std::move(session_opt)));
    app.use(route.routes());

    app.listen(10000).run();

    return 0;
}
```

#### API

##### session::options

| session::options | 类型                          | 描述                                                         | 默认值  |
| ---------------- | ----------------------------- | ------------------------------------------------------------ | ------- |
| key              | std::string                   | 用作session传递cookie的key                                   | cuehttp |
| max_age          | int                           | session传递使用cookie的max-age，单位秒，默认一天的秒数。若置为-1时，则cookie不设置max-age |         |
| auto_commit      | bool                          | session的header更新是否自动提交                              | true    |
| store            | session::store_t              | 用于配置外部存储的操作                                       |         |
| external_key     | session::external_key_t       | 用于配置外部管理key的管理                                    |         |
| genid            | std::function\<std::string()> | 外部提供key生成器，默认使用uuid                              |         |
| prefix           | std::string                   | 用于生成外部管理key的prefix                                  |         |

##### session::store_t

用于session的外部操作，若store不设置则默认时用cookie进行交互。get、set、destroy需要同时配置。

######  std::function<std::string(std::string_view)> get

传入key，返回对应session的值。

###### std::function<void(std::string_view, std::string_view, std::uint32_t)> set

传入key、value、过期时间(单位秒)来设置session。

###### std::function<void(std::string_view)> destroy

传入key，删除对应session的外部存储。

##### session::external_key_t

用于外部提供key管理。若未配置则使用cookie管理。

###### std::function\<std::string(context&)> get

获取对应HTTP会话外部提供的key。

###### std::function<void(context&, std::string_view)> set

设置对应HTTP会话的key。

###### std::function<void(context&, std::string_view)> destroy

删除对应HTTP会话的key。

### gzip

使用gzip压缩HTTP body。`使用gzip需要开启ENABLE_GZIP宏，并依赖zlib。`

#### 示例

```cpp
#include <cuehttp.hpp>

using namespace cue::http;

int main(int argc, char** argv) {
    cuehttp app;
    app.use(use_compress());
    app.use([](context& ctx) {
        ctx.type("text/html");
        ctx.body(R"(<h1>Hello cuehttp!</h1>)");
        ctx.status(200);
    });
    app.listen(10000).run();

    return 0;
}
```

#### API

##### compress::options

| compress::options | 类型          | 描述                               | 默认值 |
| ----------------- | ------------- | ---------------------------------- | ------ |
| threshold         | std::uint64_t | 配置body使用gzip压缩的临界字节大小 | 2048   |
| level             | int           | 压缩等级                           | 8      |

#####  bool compress::deflate(std::string_view src, std::string& dst, int level = 8)

内容压缩接口。

### send_file

cuehttp的静态文件发送中间件。为cuehttp提供离线文件请求支持。

#### 示例

```cpp
#include <cuehttp.hpp>

using namespace cue::http;

int main(int argc, char** argv) {
    router route;
    // 当请求http://ip:port/cpp.pptx时返回C:/Users/xcyl/Desktop/cpp11.pptx
    route.get("/cpp.pptx", [](context& ctx) {
        send::options opt;
        opt.root = "C:/Users/xcyl/Desktop/";
        send_file(ctx, "cpp11.pptx", opt);
    });

    // 当请求http://ip:port/book时返回C:/Users/xcyl/Desktop/C++Templates.pdf
    route.get("/book", [](context& ctx) {
        send_file(ctx, "C:/Users/xcyl/Desktop/C++Templates.pdf");
    });

    cuehttp app;
    app.use(route.routes());
    app.listen(10000).run();

    return 0;
}
```

#### API

##### void send_file(context& ctx, string path, [send::options options])

传入HTTP会话context对象，需要发送的文件或目录。options为可选的，当options不使用时path需要配置文件全路径。传递options时，path可以为文件或目录。

##### send::options

| send::options     | 类型                      | 描述                                                         | 默认值             |
| ----------------- | ------------------------- | ------------------------------------------------------------ | ------------------ |
| root              | std::string               | 配置发送文件的根目录                                         |                    |
| hidden            | bool                      | 是否支持目录中的隐藏文件发送，认为.开头的目录以及文件是隐藏的 | false              |
| index             | std::string               | 配置目录访问的默认文件                                       |                    |
| extensions        | std::vector\<std::string> | 配置目录中的文件访问匹配扩展名，按照内部顺序进行优先匹配     |                    |
| chunked_threshold | std::size_t                    | 文件发送Transfer-Encoding是否使用chunked，当大于此值时使用chunked，否则不配置chunked | 5,242,880(5MB大小) |
| cross_domain      | bool                      | 是否允许跨域，允许跨域时添加允许跨域header。<br/>`Access-Control-Allow-Origin: *` `Access-Control-Allow-Headers: X-Requested-With ` <br/>`Access-Control-Allow-Methods: GET,POST,OPTIONS` | false              |
| threshold | std::uint64_t | 配置body使用gzip压缩的临界字节大小 | 2048 |
| level | int | 压缩等级 | 8 |

### static

基于send_file中间件的静态文件访问中间件。

#### 示例

```cpp
#include <cuehttp.hpp>

using namespace cue::http;

int main(int argc, char** argv) {
    cuehttp app;
    app.use(use_static("C:/Users/xcyl/Desktop"));

    app.listen(10000).run();

    return 0;
}
```

#### API

##### std::function<void(context&, std::function<void()>)> use_static(string root, [static_file::options options])

生成静态文件访问中间件，root为指定目录，静态文件访问的根目录，options为相关配置(可选)。

##### static_file::options

| static_file::options | 类型                      | 描述                                                         | 默认值     |
| -------------------- | ------------------------- | ------------------------------------------------------------ | ---------- |
| hidden               | bool                      | 是否支持目录中的隐藏文件发送，认为.开头的目录以及文件是隐藏的 | false      |
| delay                | bool                      | 此中间件是否延迟执行，true为延迟为下游中间件执行后执行，否则先执行此中间件再执行下游中间件 | false      |
| index                | std::string               | 配置目录访问的默认文件                                       | index.html |
| extensions           | std::vector\<std::string> | 配置目录中的文件访问匹配扩展名，按照内部顺序进行优先匹配     |            |
| cross_domain         | bool                      | 是否允许跨域，允许跨域时添加允许跨域header。<br/>`Access-Control-Allow-Origin: *` `Access-Control-Allow-Headers: X-Requested-With ` <br/>`Access-Control-Allow-Methods: GET,POST,OPTIONS` | false      |
| threshold            | std::uint64_t             | 配置body使用gzip压缩的临界字节大小                           | 2048       |
| level                | int                       | 压缩等级                                                     | 8          |
