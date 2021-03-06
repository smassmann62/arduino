#include "M2XStreamClient.h"

#include <jsonlite.h>

#include "StreamParseFunctions.h"
#include "LocationParseFunctions.h"

const char* M2XStreamClient::kDefaultM2XHost = "api-m2x.att.com";

static int write_delete_values(Print* print, const char* from, const char* end);
int print_encoded_string(Print* print, const char* str);
int tolower(int ch);

#if defined(ARDUINO_PLATFORM) || defined(MBED_PLATFORM)
int tolower(int ch)
{
  // Arduino and mbed use ASCII table, so we can simplify the implementation
  if ((ch >= 'A') && (ch <= 'Z')) {
    return (ch + 32);
  }
  return ch;
}
#else
// For other platform, we use libc's tolower by default
#include <ctype.h>
#endif

M2XStreamClient::M2XStreamClient(Client* client,
                                 const char* key,
                                 void (* idlefunc)(void),
                                 int case_insensitive,
                                 const char* host,
                                 int port) : _client(client),
                                             _key(key),
                                             _idlefunc(idlefunc),
                                             _case_insensitive(case_insensitive),
                                             _host(host),
                                             _port(port),
                                             _null_print() {
}

int M2XStreamClient::listStreamValues(const char* deviceId, const char* streamName,
                                      stream_value_read_callback callback, void* context,
                                      const char* query) {
  if (_client->connect(_host, _port)) {
    DBGLN("%s", "Connected to M2X server!");
    _client->print("GET /v2/devices/");
    print_encoded_string(_client, deviceId);
    _client->print("/streams/");
    print_encoded_string(_client, streamName);
    _client->print("/values");

    if (query) {
      if (query[0] != '?') {
        _client->print('?');
      }
      _client->print(query);
    }

    _client->println(" HTTP/1.0");
    writeHttpHeader(-1);
  } else {
    DBGLN("%s", "ERROR: Cannot connect to M2X server!");
    return E_NOCONNECTION;
  }
  int status = readStatusCode(false);
  if (status == 200) {
    readStreamValue(callback, context);
  }

  close();
  return status;
}

int M2XStreamClient::readLocation(const char* deviceId,
                                  location_read_callback callback,
                                  void* context) {
  if (_client->connect(_host, _port)) {
    DBGLN("%s", "Connected to M2X server!");
    _client->print("GET /v2/devices/");
    print_encoded_string(_client, deviceId);
    _client->println("/location HTTP/1.0");

    writeHttpHeader(-1);
  } else {
    DBGLN("%s", "ERROR: Cannot connect to M2X server!");
    return E_NOCONNECTION;
  }
  int status = readStatusCode(false);
  if (status == 200) {
    readLocation(callback, context);
  }

  close();
  return status;
}

int M2XStreamClient::deleteValues(const char* deviceId, const char* streamName,
                                  const char* from, const char* end) {
  if (_client->connect(_host, _port)) {
    DBGLN("%s", "Connected to M2X server!");
    int length = write_delete_values(&_null_print, from, end);
    writeDeleteHeader(deviceId, streamName, length);
    write_delete_values(_client, from, end);
  } else {
    DBGLN("%s", "ERROR: Cannot connect to M2X server!");
    return E_NOCONNECTION;
  }

  return readStatusCode(true);
}

int M2XStreamClient::getTimestamp32(int32_t *ts) {
  // The maximum value of signed 64-bit integer is 0x7fffffffffffffff,
  // which is 9223372036854775807. It consists of 19 characters, so a
  // buffer of 20 is definitely enough here
  int length = 20;
  char buffer[20];
  int status = getTimestamp(buffer, &length);
  if (status == 200) {
    int32_t result = 0;
    for (int i = 0; i < length; i++) {
      result = result * 10 + (buffer[i] - '0');
    }
    if (ts != NULL) { *ts = result; }
  }
  return status;
}

int M2XStreamClient::getTimestamp(char* buffer, int *bufferLength) {
  if (bufferLength == NULL) { return E_INVALID; }
  if (_client->connect(_host, _port)) {
    DBGLN("%s", "Connected to M2X server!");
    _client->println("GET /v2/time/seconds HTTP/1.0");

    writeHttpHeader(-1);
  } else {
    DBGLN("%s", "ERROR: Cannot connect to M2X server!");
    return E_NOCONNECTION;
  }
  int status = readStatusCode(false);
  if (status == 200) {
    int length = readContentLength();
    if (length < 0) {
      close();
      return length;
    }
    if (*bufferLength < length) {
      *bufferLength = length;
      return E_BUFFER_TOO_SMALL;
    }
    *bufferLength = length;
    int index = skipHttpHeader();
    if (index != E_OK) {
      close();
      return index;
    }
    index = 0;
    while (index < length) {
      DBG("%s", "Received Data: ");
      while ((index < length) && _client->available()) {
        buffer[index++] = _client->read();
        DBG("%c", buffer[index - 1]);
      }
      DBGLNEND;

      if ((!_client->connected()) &&
          (index < length)) {
        close();
        return E_NOCONNECTION;
      }

      if (_idlefunc!=NULL) {
        _idlefunc();
      } else {
        delay(200);
      }
    }
  }
  close();
  return status;
}

static int write_delete_values(Print* print, const char* from,
                               const char* end) {
  int bytes = 0;
  bytes += print->print("{\"from\":\"");
  bytes += print->print(from);
  bytes += print->print("\",\"end\":\"");
  bytes += print->print(end);
  bytes += print->print("\"}");
  return bytes;
}

// Encodes and prints string using Percent-encoding specified
// in RFC 1738, Section 2.2
int print_encoded_string(Print* print, const char* str) {
  int bytes = 0;
  for (int i = 0; str[i] != 0; i++) {
    if (((str[i] >= 'A') && (str[i] <= 'Z')) ||
        ((str[i] >= 'a') && (str[i] <= 'z')) ||
        ((str[i] >= '0') && (str[i] <= '9')) ||
        (str[i] == '-') || (str[i] == '_') ||
        (str[i] == '.') || (str[i] == '~')) {
      bytes += print->print(str[i]);
    } else {
      // Encode all other characters
      bytes += print->print('%');
      bytes += print->print(TO_HEX(str[i] / 16));
      bytes += print->print(TO_HEX(str[i] % 16));
    }
  }
  return bytes;
}

void M2XStreamClient::writePutHeader(const char* deviceId,
                                     const char* streamName,
                                     int contentLength) {
  _client->print("PUT /v2/devices/");
  print_encoded_string(_client, deviceId);
  _client->print("/streams/");
  print_encoded_string(_client, streamName);
  _client->println("/value HTTP/1.0");

  writeHttpHeader(contentLength);
}

void M2XStreamClient::writeDeleteHeader(const char* deviceId,
                                        const char* streamName,
                                        int contentLength) {
  _client->print("DELETE /v2/devices/");
  print_encoded_string(_client, deviceId);
  _client->print("/streams/");
  print_encoded_string(_client, streamName);
  _client->print("/values");
  _client->println(" HTTP/1.0");

  writeHttpHeader(contentLength);
}

void M2XStreamClient::writeHttpHeader(int contentLength) {
  _client->println(USER_AGENT);
  _client->print("X-M2X-KEY: ");
  _client->println(_key);

  _client->print("Host: ");
  print_encoded_string(_client, _host);
  if (_port != kDefaultM2XPort) {
    _client->print(":");
    // port is an integer, does not need encoding
    _client->print(_port);
  }
  _client->println();

  if (contentLength > 0) {
    _client->println("Content-Type: application/json");
    DBG("%s", "Content Length: ");
    DBGLN("%d", contentLength);

    _client->print("Content-Length: ");
    _client->println(contentLength);
  }
  _client->println();
}

int M2XStreamClient::waitForString(const char* str) {
  int currentIndex = 0;
  if (str[currentIndex] == '\0') return E_OK;

  while (true) {
    while (_client->available()) {
      char c = _client->read();
      DBG("%c", c);

      int cmp;
      if (_case_insensitive) {
        cmp = tolower(c) - tolower(str[currentIndex]);
      } else {
        cmp = c - str[currentIndex];
      }

      if ((str[currentIndex] == '*') || (cmp == 0)) {
        currentIndex++;
        if (str[currentIndex] == '\0') {
          return E_OK;
        }
      } else {
        // start from the beginning
        currentIndex = 0;
      }
    }

    if (!_client->connected()) {
      DBGLN("%s", "ERROR: The client is disconnected from the server!");

      close();
      return E_DISCONNECTED;
    }
    if (_idlefunc!=NULL)
      _idlefunc();
    else
      delay(200);
  }
  // never reached here
  return E_NOTREACHABLE;
}

int M2XStreamClient::readStatusCode(bool closeClient) {
  int responseCode = 0;
  int ret = waitForString("HTTP/*.* ");
  if (ret != E_OK) {
    if (closeClient) close();
    return ret;
  }

  // ret is not needed from here(since it must be E_OK), so we can use it
  // as a regular variable now.
  ret = 0;
  while (true) {
    while (_client->available()) {
      char c = _client->read();
      DBG("%c", c);

      responseCode = responseCode * 10 + (c - '0');
      ret++;
      if (ret == 3) {
        if (closeClient) close();
        return responseCode;
      }
    }

    if (!_client->connected()) {
      DBGLN("%s", "ERROR: The client is disconnected from the server!");

      if (closeClient) close();
      return E_DISCONNECTED;
    }
    if (_idlefunc!=NULL)
      _idlefunc();
    else
      delay(200);
  }

  // never reached here
  return E_NOTREACHABLE;
}

int M2XStreamClient::readContentLength() {
  int ret = waitForString("Content-Length: ");
  if (ret != E_OK) {
    return ret;
  }

  // From now on, ret is not needed, we can use it
  // to keep the final result
  ret = 0;
  while (true) {
    while (_client->available()) {
      char c = _client->read();
      DBG("%c", c);

      if ((c == '\r') || (c == '\n')) {
        return (ret == 0) ? (E_INVALID) : (ret);
      } else {
        ret = ret * 10 + (c - '0');
      }
    }

    if (!_client->connected()) {
      DBGLN("%s", "ERROR: The client is disconnected from the server!");

      return E_DISCONNECTED;
    }
    if (_idlefunc!=NULL)
      _idlefunc();
    else
      delay(200);
  }

  // never reached here
  return E_NOTREACHABLE;
}

int M2XStreamClient::skipHttpHeader() {
  return waitForString("\n\r\n");
}

void M2XStreamClient::close() {
  // Eats up buffered data before closing
  _client->flush();
  _client->stop();
}

int M2XStreamClient::readStreamValue(stream_value_read_callback callback,
                                     void* context) {
  const int BUF_LEN = 64;
  char buf[BUF_LEN];

  int length = readContentLength();
  if (length < 0) {
    close();
    return length;
  }

  int index = skipHttpHeader();
  if (index != E_OK) {
    close();
    return index;
  }
  index = 0;

  stream_parsing_context_state state;
  state.state = state.index = 0;
  state.callback = callback;
  state.context = context;

  jsonlite_parser_callbacks cbs = jsonlite_default_callbacks;
  cbs.key_found = on_stream_key_found;
  cbs.number_found = on_stream_number_found;
  cbs.string_found = on_stream_string_found;
  cbs.context.client_state = &state;

  jsonlite_parser p = jsonlite_parser_init(jsonlite_parser_estimate_size(5));
  jsonlite_parser_set_callback(p, &cbs);

  jsonlite_result result = jsonlite_result_unknown;
  while (index < length) {
    int i = 0;

    DBG("%s", "Received Data: ");
    while ((i < BUF_LEN) && _client->available()) {
      buf[i++] = _client->read();
      DBG("%c", buf[i - 1]);
    }
    DBGLNEND;

    if ((!_client->connected()) &&
        (!_client->available()) &&
        ((index + i) < length)) {
      jsonlite_parser_release(p);
      close();
      return E_NOCONNECTION;
    }

    result = jsonlite_parser_tokenize(p, buf, i);
    if ((result != jsonlite_result_ok) &&
        (result != jsonlite_result_end_of_stream)) {
      jsonlite_parser_release(p);
      close();
      return E_JSON_INVALID;
    }

    index += i;
  }

  jsonlite_parser_release(p);
  close();
  return (result == jsonlite_result_ok) ? (E_OK) : (E_JSON_INVALID);
}

int M2XStreamClient::readLocation(location_read_callback callback,
                                  void* context) {
  const int BUF_LEN = 40;
  char buf[BUF_LEN];

  int length = readContentLength();
  if (length < 0) {
    close();
    return length;
  }

  int index = skipHttpHeader();
  if (index != E_OK) {
    close();
    return index;
  }
  index = 0;

  location_parsing_context_state state;
  state.state = state.index = 0;
  state.callback = callback;
  state.context = context;

  jsonlite_parser_callbacks cbs = jsonlite_default_callbacks;
  cbs.key_found = on_location_key_found;
  cbs.string_found = on_location_string_found;
  cbs.context.client_state = &state;

  jsonlite_parser p = jsonlite_parser_init(jsonlite_parser_estimate_size(5));
  jsonlite_parser_set_callback(p, &cbs);

  jsonlite_result result = jsonlite_result_unknown;
  while (index < length) {
    int i = 0;

    DBG("%s", "Received Data: ");
    while ((i < BUF_LEN) && _client->available()) {
      buf[i++] = _client->read();
      DBG("%c", buf[i - 1]);
    }
    DBGLNEND;

    if ((!_client->connected()) &&
        (!_client->available()) &&
        ((index + i) < length)) {
      jsonlite_parser_release(p);
      close();
      return E_NOCONNECTION;
    }

    result = jsonlite_parser_tokenize(p, buf, i);
    if ((result != jsonlite_result_ok) &&
        (result != jsonlite_result_end_of_stream)) {
      jsonlite_parser_release(p);
      close();
      return E_JSON_INVALID;
    }

    index += i;
  }

  jsonlite_parser_release(p);
  close();
  return (result == jsonlite_result_ok) ? (E_OK) : (E_JSON_INVALID);
}
