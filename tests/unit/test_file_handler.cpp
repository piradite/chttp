#include <gtest/gtest.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "file_handler.h"

class FileHandlerTest : public ::testing::Test {
   protected:
    void SetUp() override {
        mkdir("test_www", 0755);
        FILE* f = fopen("test_www/index.html", "w");
        fprintf(f, "hello");
        fclose(f);
        mkdir("test_www/subdir", 0755);

        FILE* f_noperm = fopen("test_www/noperm.txt", "w");
        fprintf(f_noperm, "secret");
        fclose(f_noperm);
        chmod("test_www/noperm.txt", 0000);

        FILE* f_out = fopen("outside.txt", "w");
        fprintf(f_out, "outside");
        fclose(f_out);
        symlink("../outside.txt", "test_www/sym.txt");

        ASSERT_EQ(file_handler_init("test_www"), CHTTP_OK);
    }

    void TearDown() override {
        unlink("test_www/sym.txt");
        unlink("test_www/noperm.txt");
        rmdir("test_www/subdir");
        unlink("test_www/index.html");
        rmdir("test_www");
        unlink("outside.txt");
    }
};

TEST_F(FileHandlerTest, ValidPathResolvesCorrectly) {
    char out_path[1024];
    EXPECT_EQ(file_handler_sanitize_path("/index.html", out_path, sizeof(out_path)), CHTTP_OK);
    EXPECT_NE(strstr(out_path, "test_www/index.html"), nullptr);
}

TEST_F(FileHandlerTest, EmptyPathDefaultsToIndex) {
    char out_path[1024];
    EXPECT_EQ(file_handler_sanitize_path("/", out_path, sizeof(out_path)), CHTTP_OK);
    EXPECT_NE(strstr(out_path, "test_www/index.html"), nullptr);
}

TEST_F(FileHandlerTest, PathTraversalIsRejected) {
    char out_path[1024];
    EXPECT_EQ(file_handler_sanitize_path("/../etc/passwd", out_path, sizeof(out_path)),
              CHTTP_ERR_FORBIDDEN);
}

TEST_F(FileHandlerTest, UrlEncodedTraversalRejected) {
    char out_path[1024];
    EXPECT_EQ(file_handler_sanitize_path("/../../etc/passwd", out_path, sizeof(out_path)),
              CHTTP_ERR_FORBIDDEN);
}

TEST_F(FileHandlerTest, SymlinkOutsideRootRejected) {
    char out_path[1024];
    EXPECT_EQ(file_handler_sanitize_path("/sym.txt", out_path, sizeof(out_path)),
              CHTTP_ERR_FORBIDDEN);
}

TEST_F(FileHandlerTest, RequestForDirectoryHandled) {
    char out_path[1024];
    EXPECT_EQ(file_handler_sanitize_path("/subdir", out_path, sizeof(out_path)), CHTTP_OK);
}

TEST_F(FileHandlerTest, MimeTypesDeducedCorrectly) {
    EXPECT_STREQ(file_handler_get_mime_type("test.html"), "text/html");
    EXPECT_STREQ(file_handler_get_mime_type("style.css"), "text/css");
    EXPECT_STREQ(file_handler_get_mime_type("app.js"), "application/javascript");
    EXPECT_STREQ(file_handler_get_mime_type("image.png"), "image/png");
    EXPECT_STREQ(file_handler_get_mime_type("data.json"), "application/json");
    EXPECT_STREQ(file_handler_get_mime_type("unknown.bin"), "application/octet-stream");
    EXPECT_STREQ(file_handler_get_mime_type("noext"), "application/octet-stream");
}

TEST_F(FileHandlerTest, FileNotReadableReturns403) {
    const char* noperm_path = "test_www/noperm.txt";
    FILE* f = fopen(noperm_path, "w");
    if (f) {
        fputs("secret", f);
        fclose(f);
        chmod(noperm_path, 0000);
    }

    HttpRequest* req;
    HttpResponse* res;
    http_request_create(&req);
    http_response_create(&res);

    const char* buf = "GET /noperm.txt HTTP/1.1\r\nHost: a\r\n\r\n";
    size_t parsed = 0;
    http_request_parse(req, buf, strlen(buf), &parsed);

    EXPECT_EQ(file_handler_process_request(req, res), CHTTP_OK);

    char out_buf[1024];
    size_t out_len = 0;
    http_response_serialize_headers(res, out_buf, sizeof(out_buf), &out_len);
    EXPECT_NE(strstr(out_buf, "403 Forbidden"), nullptr);

    http_request_destroy(req);
    http_response_destroy(res);

    chmod(noperm_path, 0644);
    unlink(noperm_path);
}
