#include "dl_lib_matrix3d.h"
#include <esp32-hal-ledc.h>
int speed = 255;

#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "Arduino.h"

typedef struct {
  httpd_req_t *req;
  size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

static size_t jpg_encode_stream(void * arg, size_t index, const void* data, size_t len) {
  jpg_chunking_t *j = (jpg_chunking_t *)arg;
  if (!index) {
    j->len = 0;
  }
  if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) {
    return 0;
  }
  j->len += len;
  return len;
}

static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  int64_t fr_start = esp_timer_get_time();

  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");

  size_t out_len, out_width, out_height;
  uint8_t * out_buf;
  bool s;
  {
    size_t fb_len = 0;
    if (fb->format == PIXFORMAT_JPEG) {
      fb_len = fb->len;
      res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    } else {
      jpg_chunking_t jchunk = {req, 0};
      res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
      httpd_resp_send_chunk(req, NULL, 0);
      fb_len = jchunk.len;
    }
    esp_camera_fb_return(fb);
    int64_t fr_end = esp_timer_get_time();
    Serial.printf("JPG: %uB %ums\n", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start) / 1000));
    return res;
  }

  dl_matrix3du_t *image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
  if (!image_matrix) {
    esp_camera_fb_return(fb);
    Serial.println("dl_matrix3du_alloc failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  out_buf = image_matrix->item;
  out_len = fb->width * fb->height * 3;
  out_width = fb->width;
  out_height = fb->height;

  s = fmt2rgb888(fb->buf, fb->len, fb->format, out_buf);
  esp_camera_fb_return(fb);
  if (!s) {
    dl_matrix3du_free(image_matrix);
    Serial.println("to rgb888 failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  jpg_chunking_t jchunk = {req, 0};
  s = fmt2jpg_cb(out_buf, out_len, out_width, out_height, PIXFORMAT_RGB888, 90, jpg_encode_stream, &jchunk);
  dl_matrix3du_free(image_matrix);
  if (!s) {
    Serial.println("JPEG compression failed");
    return ESP_FAIL;
  }

  int64_t fr_end = esp_timer_get_time();
  return res;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];
  dl_matrix3du_t *image_matrix = NULL;

  static int64_t last_frame = 0;
  if (!last_frame) {
    last_frame = esp_timer_get_time();
  }

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      {
        if (fb->format != PIXFORMAT_JPEG) {
          bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
          esp_camera_fb_return(fb);
          fb = NULL;
          if (!jpeg_converted) {
            Serial.println("JPEG compression failed");
            res = ESP_FAIL;
          }
        } else {
          _jpg_buf_len = fb->len;
          _jpg_buf = fb->buf;
        }
      }
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) {
      break;
    }
    int64_t fr_end = esp_timer_get_time();
    int64_t frame_time = fr_end - last_frame;
    last_frame = fr_end;
    frame_time /= 1000;
    /*Serial.printf("MJPG: %uB %ums (%.1ffps)\n",
                  (uint32_t)(_jpg_buf_len),
                  (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time
                 );*/
  }

  last_frame = 0;
  return res;
}


static esp_err_t cmd_handler(httpd_req_t *req)
{
  char*  buf;
  size_t buf_len;
  char variable[32] = {0,};
  char value[32] = {0,};

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char*)malloc(buf_len);
    if (!buf) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) == ESP_OK &&
          httpd_query_key_value(buf, "val", value, sizeof(value)) == ESP_OK) {
      } else {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
      }
    } else {
      free(buf);
      httpd_resp_send_404(req);
      return ESP_FAIL;
    }
    free(buf);
  } else {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  int val = atoi(value);
  sensor_t * s = esp_camera_sensor_get();
  int res = 0;

  if (!strcmp(variable, "framesize"))
  {
    Serial.println("framesize");
    if (s->pixformat == PIXFORMAT_JPEG) res = s->set_framesize(s, (framesize_t)val);
  }
  else if (!strcmp(variable, "quality"))
  {
    Serial.println("quality");
    res = s->set_quality(s, val);
  }
  //Remote Control Car
  //Don't use channel 1 and channel 2
  else if (!strcmp(variable, "flash"))
  {
    ledcWrite(7, val);
  }
  else if (!strcmp(variable, "speed"))
  {
    /* Setting the motor speed */
    if      (val > 255) val = 255;
    else if (val <   0) val = 0;
    speed = val;
  }

  else if (!strcmp(variable, "servo"))
  {
    /* Controlling the servo motor angle with PWM */
    /* ledcWrite(Channel, Dutycycle) */
    if      (val > 650) val = 650;
    else if (val < 225) val = 325;
    ledcWrite(8, 10 * val);
  }
  else if (!strcmp(variable, "car")) {
    if (val == 1) {
      Serial.println("Frente");
      /* Controlling the motor with PWM */
      /* ledcWrite(Channel, Dutycycle) */
      ledcWrite(3, 0);
      ledcWrite(4, 0);
      ledcWrite(5, 0);
      ledcWrite(6, 255);
    }
    else if (val == 2) {
      Serial.println("GiroDireita");
      /* Controlling the motor with PWM */
      /* ledcWrite(Channel, Dutycycle) */
      ledcWrite(3, 0);
      ledcWrite(4, 0);
      ledcWrite(5, 255);
      ledcWrite(6, 0);
    }
    else if (val == 3) {
      Serial.println("Stop");
      /* Controlling the motor with PWM */
      /* ledcWrite(Channel, Dutycycle) */
      ledcWrite(3, 0);
      ledcWrite(4, 0);
      ledcWrite(5, 255);
      ledcWrite(6, 255);
    }
    else if (val == 4) {
      Serial.println("GiroEsquerda");
      /* Controlling the motor with PWM */
      /* ledcWrite(Channel, Dutycycle) */
      ledcWrite(3, 0);
      ledcWrite(4, 255);
      ledcWrite(5, 0);
      ledcWrite(6, 0);

    }
    else if (val == 5) {
      Serial.println("R??");
      /* Controlling the motor with PWM */
      /* ledcWrite(Channel, Dutycycle) */
      ledcWrite(3, 0);
      ledcWrite(4, 255);
      ledcWrite(5, 0);
      ledcWrite(6, 255);
    }
    else if (val == 6) {
      Serial.println("Direita");
      /* Controlling the motor with PWM */
      /* ledcWrite(Channel, Dutycycle) */
      ledcWrite(3, 0);
      ledcWrite(4, 255);
      ledcWrite(5, 255);
      ledcWrite(6, 0);
    }
    else if (val == 7) {
      Serial.println("Esquerda");
      /* Controlling the motor with PWM */
      /* ledcWrite(Channel, Dutycycle) */
      ledcWrite(3, 0);
      ledcWrite(4, 255);
      ledcWrite(5, 255);
      ledcWrite(6, 255);
    }
  }
  else
  {
    Serial.println("variable");
    res = -1;
  }

  if (res) {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req) {
  static char json_response[1024];

  sensor_t * s = esp_camera_sensor_get();
  char * p = json_response;
  *p++ = '{';

  p += sprintf(p, "\"framesize\":%u,", s->status.framesize);
  p += sprintf(p, "\"quality\":%u,", s->status.quality);
  *p++ = '}';
  *p++ = 0;
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json_response, strlen(json_response));
}

/* Index page */
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!doctype html>
<html>

<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <title>Rob??</title>
    <style>

html {
    user-select: none;
}

button {
 padding: 15px 25px;
 border: unset;
 border-radius: 15px;
 color: #212121;
 z-index: 1;
 background: #e8e8e8;
 position: relative;
 font-weight: 1000;
 font-size: 17px;
 -webkit-box-shadow: 4px 8px 19px -3px rgba(0,0,0,0.27);
 box-shadow: 4px 8px 19px -3px rgba(0,0,0,0.27);
 transition: all 250ms;
 overflow: hidden;
}

button::before {
 content: "";
 position: absolute;
 top: 0;
 left: 0;
 height: 100%;
 width: 0;
 border-radius: 15px;
 background-color: #212121;
 z-index: -1;
 -webkit-box-shadow: 4px 8px 19px -3px rgba(0,0,0,0.27);
 box-shadow: 4px 8px 19px -3px rgba(0,0,0,0.27);
 transition: all 250ms
}

button:hover {
 color: #e8e8e8;
}

button:hover::before {
 width: 100%;
}


        .slider {
            appearance: none;
            width: 70%;
            height: 15px;
            border-radius: 10px;
            background: #d3d3d3;
            outline: none;
        }

        .slider::-webkit-slider-thumb {
            appearance: none;
            appearance: none;
            width: 30px;
            height: 30px;
            border-radius: 50%;
            background: #f7190a;
        }

        .label {
            color: #f7190a;
            font-size: 18px;
        }

    body,html{
        height: 100%;
        margin: 0;
    }

    .bg{
        background-color: #6b6868;
        background-image: url(https://cdn.wallpapersafari.com/38/13/sQtEFD.png);
        background-repeat: no-repeat;
        background-size: cover;
        background-position: left;
        background-attachment: fixed;
    }
    </style>
</head>

<body class="bg">
    <div align=center> <img src='http://192.168.15.7:81/stream'
            style='width:100%; transform:rotate(180deg);'></div>
    <br />
    <br />
    <div align=center>
        <button class="button" id="forward" onclick="fetch(document.location.origin+'/control?var=car&val=1');"
            ontouchend="fetch(document.location.origin+'/control?var=car&val=3');"><span></span>
            <span></span>
            <span></span>
            <span></span>F</button>
    </div>
    <br />
    <div align=center>
        <button class="button" id="turnleft" onclick="fetch(document.location.origin+'/control?var=car&val=4');"
            ontouchend="fetch(document.location.origin+'/control?var=car&val=3');"><span></span>
            <span></span>
            <span></span>
            <span></span>Giro E</button>
        <button class="button" id="stop"
            onclick="fetch(document.location.origin+'/control?var=car&val=3');"><span></span>
            <span></span>
            <span></span>
            <span></span>Stop</button>
        <button class="button" id="turnright" onclick="fetch(document.location.origin+'/control?var=car&val=2');"
            ontouchend="fetch(document.location.origin+'/control?var=car&val=3');"><span></span>
            <span></span>
            <span></span>
            <span></span>Giro D</button>
    </div>
    <br />
    <div align=center>

        <button class="button" id="backward1" onclick="fetch(document.location.origin+'/control?var=car&val=7');"
            ontouchend="fetch(document.location.origin+'/control?var=car&val=3');"><span></span>
            <span></span>
            <span></span>
            <span></span>E</button>


        <button class="button" id="backward" onclick="fetch(document.location.origin+'/control?var=car&val=5');"
            ontouchend="fetch(document.location.origin+'/control?var=car&val=3');"><span></span>
            <span></span>
            <span></span>
            <span></span>R</button>

        <button class="button" id="backward2" onclick="fetch(document.location.origin+'/control?var=car&val=6');"
            ontouchend="fetch(document.location.origin+'/control?var=car&val=3');">  <span></span>
            <span></span>
            <span></span>
            <span></span>D</button>


    </div>
    <br />
    <div align=center>
        <label class="label">Led</label>
        <input type="range" class="slider" id="flash" min="0" max="255" value="0"
            onchange="try{fetch(document.location.origin+'/control?var=flash&val='+this.value);}catch(e){}">
    </div>
    <br />
    <div align=center>
        <label class="label">Velocidade</label>
        <input type="range" class="slider" id="speed" min="0" max="255" value="255"
            onchange="try{fetch(document.location.origin+'/control?var=speed&val='+this.value);}catch(e){}">
    </div>
    <br />
    <!-- 
    <div align=center>
        <label class="label">Servo</label>
        <input type="range" class="slider" id="servo" min="325" max="650" value="487"
            onchange="try{fetch(document.location.origin+'/control?var=servo&val='+this.value);}catch(e){}">
    </div>
    -->
</body>

</html>
)rawliteral";

static esp_err_t index_handler(httpd_req_t *req){
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

void startCameraServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t status_uri = {
        .uri       = "/status",
        .method    = HTTP_GET,
        .handler   = status_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t cmd_uri = {
        .uri       = "/control",
        .method    = HTTP_GET,
        .handler   = cmd_handler,
        .user_ctx  = NULL
    };

   httpd_uri_t stream_uri = {
        .uri       = "/stream",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };
    
    Serial.printf("Starting web server on port: '%d'\n", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    Serial.printf("Starting stream server on port: '%d'\n", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}
