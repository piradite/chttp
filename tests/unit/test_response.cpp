#include <gtest/gtest.h>

#include "http_response.h"

class HttpResponseTest : public ::testing::Test {
   protected:
    HttpResponse* res;

    void SetUp() override { ASSERT_EQ(http_response_create(&res), CHTTP_OK); }

    void TearDown() override { http_response_destroy(res); }
};

TEST_F(HttpResponseTest, SerializationWorks) {
    http_response_set_status(res, 200, false);
    http_response_add_header(res, "Content-Type", "text/plain");
    http_response_add_header(res, "Content-Length", "12");

    char buf[1024];
    size_t out_len = 0;
    EXPECT_EQ(http_response_serialize_headers(res, buf, sizeof(buf), &out_len), CHTTP_OK);

    EXPECT_NE(strstr(buf, "HTTP/1.1 200 OK\r\n"), nullptr);
    EXPECT_NE(strstr(buf, "Connection: keep-alive\r\n"), nullptr);
    EXPECT_NE(strstr(buf, "Content-Type: text/plain\r\n"), nullptr);
    EXPECT_NE(strstr(buf, "\r\n\r\n"), nullptr);
}

TEST_F(HttpResponseTest, BufferTooSmall) {
    http_response_set_status(res, 200, false);
    http_response_add_header(res, "Content-Type", "text/plain");

    char buf[32];
    size_t out_len = 0;
    EXPECT_EQ(http_response_serialize_headers(res, buf, sizeof(buf), &out_len), CHTTP_ERR_TOOLARGE);
}

TEST_F(HttpResponseTest, AllStatusCodesProduceCorrectStrings) {
    int codes[] = {200, 400, 403, 404, 405, 500};
    const char* expected[] = {"HTTP/1.1 200 OK\r\n",
                              "HTTP/1.1 400 Bad Request\r\n",
                              "HTTP/1.1 403 Forbidden\r\n",
                              "HTTP/1.1 404 Not Found\r\n",
                              "HTTP/1.1 405 Method Not Allowed\r\n",
                              "HTTP/1.1 500 Internal Server Error\r\n"};

    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        HttpResponse* tmp_res;
        ASSERT_EQ(http_response_create(&tmp_res), CHTTP_OK);
        http_response_set_status(tmp_res, codes[i], false);

        char buf[1024];
        size_t out_len = 0;
        EXPECT_EQ(http_response_serialize_headers(tmp_res, buf, sizeof(buf), &out_len), CHTTP_OK);
        EXPECT_NE(strstr(buf, expected[i]), nullptr);
        http_response_destroy(tmp_res);
    }
}

TEST_F(HttpResponseTest, ConnectionCloseHeaderSet) {
    http_response_set_status(res, 200, true);
    char buf[1024];
    size_t out_len = 0;
    EXPECT_EQ(http_response_serialize_headers(res, buf, sizeof(buf), &out_len), CHTTP_OK);
    EXPECT_NE(strstr(buf, "Connection: close\r\n"), nullptr);
}

TEST_F(HttpResponseTest, ContentLengthAndTypeIncluded) {
    http_response_set_status(res, 200, false);
    http_response_add_header(res, "Content-Type", "application/json");
    http_response_add_header(res, "Content-Length", "42");

    char buf[1024];
    size_t out_len = 0;
    EXPECT_EQ(http_response_serialize_headers(res, buf, sizeof(buf), &out_len), CHTTP_OK);
    EXPECT_NE(strstr(buf, "Content-Type: application/json\r\n"), nullptr);
    EXPECT_NE(strstr(buf, "Content-Length: 42\r\n"), nullptr);
}

TEST_F(HttpResponseTest, ResponseWithBodyIsCorrect) {
    http_response_set_status(res, 200, false);
    FileBody body = {.fd = 123, .size = 456};
    http_response_set_file_body(res, body);
    EXPECT_EQ(http_response_get_file_fd(res), 123);
    EXPECT_EQ(http_response_get_file_size(res), 456);
}

TEST_F(HttpResponseTest, KeepAliveHeaderSet) {
    http_response_set_status(res, 200, false);
    char buf[1024];
    size_t out_len = 0;
    EXPECT_EQ(http_response_serialize_headers(res, buf, sizeof(buf), &out_len), CHTTP_OK);
    EXPECT_NE(strstr(buf, "Connection: keep-alive\r\n"), nullptr);
}

TEST_F(HttpResponseTest, DateHeaderPresentAndFormatValid) {
    http_response_set_status(res, 200, false);
    char buf[1024];
    size_t out_len = 0;
    EXPECT_EQ(http_response_serialize_headers(res, buf, sizeof(buf), &out_len), CHTTP_OK);

    const char* date_ptr = strstr(buf, "Date: ");
    EXPECT_NE(date_ptr, nullptr);
    const char* gmt_ptr = strstr(date_ptr, " GMT\r\n");
    EXPECT_NE(gmt_ptr, nullptr);
}

TEST_F(HttpResponseTest, Response405IncludesAllowHeader) {
    http_response_set_status(res, 405, true);
    http_response_add_header(res, "Allow", "GET, HEAD, POST");
    char buf[1024];
    size_t out_len = 0;
    EXPECT_EQ(http_response_serialize_headers(res, buf, sizeof(buf), &out_len), CHTTP_OK);
    EXPECT_NE(strstr(buf, "Allow: GET, HEAD, POST\r\n"), nullptr);
    EXPECT_NE(strstr(buf, "405 Method Not Allowed"), nullptr);
}
