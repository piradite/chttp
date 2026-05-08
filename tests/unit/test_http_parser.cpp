#include <gtest/gtest.h>

#include "http_parser.h"

class HttpParserTest : public ::testing::Test {
   protected:
    HttpRequest* req;

    void SetUp() override { ASSERT_EQ(http_request_create(&req), CHTTP_OK); }

    void TearDown() override { http_request_destroy(req); }
};

TEST_F(HttpParserTest, ValidGetRequest) {
    const char* buf = "GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_OK);
    EXPECT_EQ(parsed, strlen(buf));
    EXPECT_EQ(http_request_get_method(req), HTTP_METHOD_GET);
    EXPECT_STREQ(http_request_get_path(req), "/index.html");
    EXPECT_STREQ(http_request_get_header(req, "Host"), "localhost");
}

TEST_F(HttpParserTest, ValidPostRequest) {
    const char* buf = "POST /api HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\nhello";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_OK);
    EXPECT_EQ(http_request_get_method(req), HTTP_METHOD_POST);
    EXPECT_STREQ(http_request_get_path(req), "/api");
    EXPECT_STREQ(http_request_get_header(req, "Content-Length"), "5");
}

TEST_F(HttpParserTest, MissingHostHeaderReturnsBadRequest) {
    const char* buf = "GET / HTTP/1.1\r\nAccept: */*\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_ERR_BAD_REQUEST);
}

TEST_F(HttpParserTest, Http10RequestAcceptedWithConnectionClose) {
    const char* buf = "GET / HTTP/1.0\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_OK);
    EXPECT_TRUE(http_request_should_close(req));
}

TEST_F(HttpParserTest, TruncatedRequestReturnsIncomplete) {
    const char* buf = "GET / HTTP/1.1\r\nHost: loc";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_ERR_INCOMPLETE);
}

TEST_F(HttpParserTest, HeaderWithNoValue) {
    const char* buf = "GET / HTTP/1.1\r\nHost: localhost\r\nX-Empty:\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_OK);
    EXPECT_STREQ(http_request_get_header(req, "X-Empty"), "");
}

TEST_F(HttpParserTest, RequestWithTooManyHeadersRejected) {
    std::string req_str = "GET / HTTP/1.1\r\nHost: localhost\r\n";
    for (int i = 0; i < 105; ++i) {
        req_str += "X-Header-" + std::to_string(i) + ": val\r\n";
    }
    req_str += "\r\n";

    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, req_str.c_str(), req_str.length(), &parsed),
              CHTTP_ERR_BAD_REQUEST);
}

TEST_F(HttpParserTest, HeadMethodParsed) {
    const char* buf = "HEAD / HTTP/1.1\r\nHost: a\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_OK);
    EXPECT_EQ(http_request_get_method(req), HTTP_METHOD_HEAD);
}

TEST_F(HttpParserTest, UnknownMethodRejected) {
    const char* buf = "PUT / HTTP/1.1\r\nHost: a\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_ERR_METHOD_NOT_ALLOWED);
}

TEST_F(HttpParserTest, PathTooLongRejected) {
    std::string req_str = "GET /";
    for (int i = 0; i < 1024; ++i) req_str += "a";
    req_str += " HTTP/1.1\r\nHost: a\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, req_str.c_str(), req_str.length(), &parsed),
              CHTTP_ERR_TOOLARGE);
}

TEST_F(HttpParserTest, InvalidHttpVersionRejected) {
    const char* buf = "GET / HTTP/2.0\r\nHost: a\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_ERR_BAD_REQUEST);
}

TEST_F(HttpParserTest, MissingPathRejected) {
    const char* buf = "GET HTTP/1.1\r\nHost: a\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_ERR_BAD_REQUEST);
}

TEST_F(HttpParserTest, HeaderValueWithLeadingSpaces) {
    const char* buf = "GET / HTTP/1.1\r\nHost:   localhost\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_OK);
    EXPECT_STREQ(http_request_get_header(req, "Host"), "localhost");
}

TEST_F(HttpParserTest, MultipleHeadersParsed) {
    const char* buf =
        "GET / HTTP/1.1\r\nHost: a\r\nAccept: text/html\r\nConnection: keep-alive\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_OK);
    EXPECT_STREQ(http_request_get_header(req, "Accept"), "text/html");
    EXPECT_STREQ(http_request_get_header(req, "Connection"), "keep-alive");
}

TEST_F(HttpParserTest, ConnectionKeepAliveFlag) {
    const char* buf = "GET / HTTP/1.1\r\nHost: a\r\nConnection: keep-alive\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_OK);
    EXPECT_FALSE(http_request_should_close(req));
}

TEST_F(HttpParserTest, ConnectionCloseFlag) {
    const char* buf = "GET / HTTP/1.1\r\nHost: a\r\nConnection: close\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_OK);
    EXPECT_TRUE(http_request_should_close(req));
}

TEST_F(HttpParserTest, CaseInsensitiveHeaderLookup) {
    const char* buf = "GET / HTTP/1.1\r\nHoSt: a\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_OK);
    EXPECT_STREQ(http_request_get_header(req, "host"), "a");
}

TEST_F(HttpParserTest, NullRequestMethodsSafe) {
    EXPECT_EQ(http_request_get_method(nullptr), HTTP_METHOD_UNKNOWN);
    EXPECT_EQ(http_request_get_path(nullptr), nullptr);
    EXPECT_EQ(http_request_get_header(nullptr, "Host"), nullptr);
    EXPECT_TRUE(http_request_should_close(nullptr));
}

TEST_F(HttpParserTest, EmptyLineInHeadersIgnoredOrTreatedAsEnd) {
    const char* buf = "GET / HTTP/1.1\r\nHost: a\r\n\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_OK);
}

TEST_F(HttpParserTest, HeaderKeyTooLongRejected) {
    std::string req_str = "GET / HTTP/1.1\r\nHost: a\r\n";
    for (int i = 0; i < 150; ++i) req_str += "K";
    req_str += ": value\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, req_str.c_str(), req_str.length(), &parsed),
              CHTTP_ERR_BAD_REQUEST);
}

TEST_F(HttpParserTest, MalformedHeaderLineMissingColonRejected) {
    const char* buf = "GET / HTTP/1.1\r\nHost a\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_ERR_BAD_REQUEST);
}

TEST_F(HttpParserTest, DuplicateContentLengthRejected) {
    const char* buf =
        "POST /api HTTP/1.1\r\nHost: a\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\nhello";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_ERR_BAD_REQUEST);
}

TEST_F(HttpParserTest, UrlEncodingDecodedAndQueryStripped) {
    const char* buf = "GET /folder/my%20file%2Etxt?query=123 HTTP/1.1\r\nHost: a\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_OK);
    EXPECT_STREQ(http_request_get_path(req), "/folder/my file.txt");
}

TEST_F(HttpParserTest, NullByteInjectionInUrlRejected) {
    const char* buf = "GET /file%00.txt HTTP/1.1\r\nHost: a\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_ERR_BAD_REQUEST);
}

TEST_F(HttpParserTest, ContentLengthParsedAndBodyStored) {
    const char* buf = "POST /api HTTP/1.1\r\nHost: localhost\r\nContent-Length: 6\r\n\r\nbody!!";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_OK);
    EXPECT_EQ(parsed, strlen(buf));
    EXPECT_EQ(http_request_get_body_len(req), 6);
    EXPECT_STREQ(http_request_get_body(req), "body!!");
}

TEST_F(HttpParserTest, MalformedContentLengthRejected) {
    const char* buf = "POST /api HTTP/1.1\r\nHost: localhost\r\nContent-Length: -5\r\n\r\nbody";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_ERR_BAD_REQUEST);

    const char* buf2 = "POST /api HTTP/1.1\r\nHost: localhost\r\nContent-Length: abc\r\n\r\nbody";
    EXPECT_EQ(http_request_parse(req, buf2, strlen(buf2), &parsed), CHTTP_ERR_BAD_REQUEST);
}

TEST_F(HttpParserTest, IncompleteBodyReturnsIncomplete) {
    const char* buf = "POST /api HTTP/1.1\r\nHost: localhost\r\nContent-Length: 10\r\n\r\npart";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_ERR_INCOMPLETE);
}

TEST_F(HttpParserTest, BodyExceedsMaxSizeRejected) {
    const char* buf = "POST /api HTTP/1.1\r\nHost: a\r\nContent-Length: 999999999\r\n\r\nbody";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_ERR_TOOLARGE);
}

TEST_F(HttpParserTest, ContentLengthMismatchRejected) {
    const char* buf = "POST /api HTTP/1.1\r\nHost: a\r\nContent-Length: 10\r\n\r\n12345";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_ERR_INCOMPLETE);
}

TEST_F(HttpParserTest, ZeroContentLengthWithBodyIgnored) {
    const char* buf = "POST /api HTTP/1.1\r\nHost: a\r\nContent-Length: 0\r\n\r\nextrabytes";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_OK);
    EXPECT_EQ(http_request_get_body_len(req), 0);
    EXPECT_EQ(parsed, strstr(buf, "\r\n\r\n") - buf + 4);
}

TEST_F(HttpParserTest, HeaderValueTooLongRejected) {
    std::string req_str = "GET / HTTP/1.1\r\nHost: a\r\nLong-Header: ";
    for (int i = 0; i < 9000; ++i) req_str += "V";
    req_str += "\r\n\r\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, req_str.c_str(), req_str.length(), &parsed),
              CHTTP_ERR_BAD_REQUEST);
}

TEST_F(HttpParserTest, LfOnlyLineEndingsHandled) {
    const char* buf = "GET / HTTP/1.1\nHost: a\n\n";
    size_t parsed = 0;
    EXPECT_EQ(http_request_parse(req, buf, strlen(buf), &parsed), CHTTP_ERR_BAD_REQUEST);
}
