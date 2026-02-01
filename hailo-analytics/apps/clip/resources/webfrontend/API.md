---

# New CLIP Server API Documentation

This document outlines the API endpoints for the Integrated CLIP Web Application. The API provides services for configuration, CLIP model interaction, real-time video streaming via WebRTC, and storage monitoring.

## Table of Contents
1.  [General & Configuration API](#general--configuration-api)
2.  [CLIP Model & Query API](#clip-model--query-api)
3.  [WebRTC Streaming API](#webrtc-streaming-api)
4.  [Storage API](#storage-api)

---

## 1. General & Configuration API

These endpoints handle the general status of the server and dynamic configuration settings.

### Get Server Status

Checks the basic health and status of the web server.

-   **Endpoint:** `GET /api/status`
-   **Method:** `GET`
-   **Request Body:** None
-   **Success Response (200 OK):**
    ```json
    {
        "status": "ok",
        "timestamp": 1678886400000
    }
    ```
    -   `status` (string): Always "ok" if the server is running.
    -   `timestamp` (number): Current server time as a UNIX timestamp in milliseconds.

---

### Get Image Embedding Refresh Rate

Retrieves the current refresh rate for the tracked image embedding process in the video pipeline.

-   **Endpoint:** `GET /api/config/embedded_refresh`
-   **Method:** `GET`
-   **Request Body:** None
-   **Success Response (200 OK):**
    ```json
    {
        "rate": 30
    }
    ```
    -   `rate` (number): The current refresh rate in frames per second (FPS).
-   **Error Response (500 Internal Server Error):**
    ```json
    {
        "error": "AppControlServiceExt extension not available"
    }
    ```

### Set Image Embedding Refresh Rate

Sets a new refresh rate for the tracked image embedding process.

-   **Endpoint:** `POST /api/config/embedded_refresh`
-   **Method:** `POST`
-   **Request Body:**
    ```json
    {
        "rate": 15
    }
    ```
    -   `rate` (number): The new desired refresh rate (FPS). Currently 0 is invalid, although upper limit not set by backend but it should be limited in frontend within range 1 to 60. Default to 15 is normal use case for application.
-   **Success Response (200 OK):**
    ```json
    {
        "status": "ok",
        "new_rate": 15
    }
    ```
-   **Error Response (500 Internal Server Error):**
    -   If extension module not available: `{ "error": "AppControlServiceExt extension not available" }`
-   **Error Response (400 Bad Request):**
    -   If rate is invalid: `{ "error": "Invalid refresh rate" }`

---

### Get Query Video Playback Length

Retrieves the total duration for video clips playback generated for the selected query results.

-   **Endpoint:** `GET /api/config/video_playback_total_length`
-   **Method:** `GET`
-   **Request Body:** None
-   **Success Response (200 OK):**
    ```json
    {
        "total_length": 15
    }
    ```
    -   `total_length` (number): The total video playback duration in **seconds**.
-   **Error Response (500 Internal Server Error):**
    ```json
    {
        "error": "ClipQueryService extension not available"
    }
    ```

### Set Query Video Playback Length

Sets a new total duration for video clips generated for query results.

-   **Endpoint:** `POST /api/config/video_playback_total_length`
-   **Method:** `POST`
-   **Request Body:**
    ```json
    {
        "total_length": 20
    }
    ```
    -   `total_length` (number): The new desired duration in **seconds**. Currently backend limits the length between **10 to 60 seconds**.
-   **Success Response (200 OK):**
    ```json
    {
        "status": "ok",
        "new_total_length": 20
    }
    ```
-   **Error Response (500 Internal Server Error):**
    -   If extension module not available: `{ "error": "ClipQueryService extension not available" }`
-   **Error Response (400 Bad Request):**
    -   If length is invalid: `{ "error": "Invalid video playback total length" }`

---

## 2. CLIP Model & Query API

These endpoints are for interacting with the CLIP text encoder models and performing similarity searches.

### List Available CLIP Networks

Fetches a list of all available text encoder models configured on the server.

-   **Endpoint:** `GET /api/networks`
-   **Method:** `GET`
-   **Request Body:** None
-   **Success Response (200 OK):** An array of network objects.
    ```json
    [
        {
            "name": "CLIP ViT-B/32",
            "id": "clip_vit_b32",
            "onnx_file_path": "resources/models/clip_vit_b32.onnx",
            "embedding_size": 512,
            "context_length": 77
        },
        {
            "name": "Other CLIP Model",
            "id": "other_clip_model",
            "onnx_file_path": "resources/models/other.onnx",
            "embedding_size": 768,
            "context_length": 77
        }
    ]
    ```
    -   `name` (string): The name to show in the list.
    -   `id` (string): The unique identification which is used to identify the clip model and used when requesting for query.
    -   `onnx_file_path` (string): The network file path which is used to request file upload from backend which frontend will be used for text encoding on browser. **DEVELOPER NOTE:** This is for early development, now we already have implemented text encoding on Hailo15, however, this is useful for us to cross check the result (accuracy) between text encoding using the original network onxx vs hailo hef. We can discuss if we want to completely remove this.
    -   `embedding_size` (number): the embedding size of the network.
    -   `context_length` (number): the context/token length of the network.

### Download ONNX Model

Downloads the binary .onnx model file for a specific network. This allows the frontend to perform text encoding locally in the browser. DEVELOPER NOTE: This API and its relative inference feature can be skipped if we do not plan to add this in our official frontend.

-   **Endpoint:** `GET /api/models/{model_id}`
-   **Method:** `GET`
-   **URL Parameter:**
    -   `model_id` (string): The unique ID of the model (e.g., `clip_vit_b32`) from the `/api/networks` response.
-   **Request Body:** None
-   **Success Response (200 OK):**

    -   **Content-Type:** `application/octet-stream`
    -   **Body:** The raw binary data of the `.onnx` model file.

-   **Error Response (404 Not Found):** If the model file for the given ID does not exist.
-   **Error Response (500 Internal Server Error):** if Failed to read model file

### Submit Query

Submits text embeddings to the server to find matching images in the database.

-   **Endpoint:** `POST /api/embedding`
-   **Method:** `POST`
-   **Request Body:**
    ```json
    {
      "network_id": "clip_vit_b32",
      "positive_prompt": {
        "text": "a photo of a person drinking water",
        "embedding": [0.1, -0.23, ..., 0.05]
      },
      "negative_prompts": [
        {
          "text": "a photo of a person",
          "embedding": [-0.5, 0.11, ..., 0.4]
        }
      ],
      "score_threshold": 0.9,
      "max_query": 20,
      "remove_duplicate_within_sec": 60,
      "text_decode_on_device": true
    }
    ```
    -   `network_id` (string): The ID of the model to use for the text prompt for query.
    -   `positive_prompt` (object): The main search query.
        -   `text` (string): The user's input text.
        -   `embedding` (array of numbers): The normalized float vector generated from the text. **DEVELOPER NOTE:** This is the text encoded embedding from the inference result from browser using the uploaded onnx. We can discuss to remove this or simply set to [0.0].
    -   `negative_prompts` (array of objects, optional): An array of prompts to exclude from the results. Each object has the same structure as `positive_prompt`. **DEVELOPER NOTE:** strongly recommend to always have one default negative prompt `a photo of a person`, user can chose to edit it or add more but there should be at least one and we should provide the default.
    -   `score_threshold` (number): A value between 0.0 and 1.0. Only results with a cosine similarity score above this threshold will be returned.
    -   `max_query` (number): The maximum number of results to return.
    -   `remove_duplicate_within_sec` (number): Time in seconds to group similar results to avoid duplicates.
    -   `text_decode_on_device` (boolean): Flag indicating if the embedding will be generated from Hailo15 (`true`) or use the provided embedding from browser (`false`). **DEVELOPED NOTE:** We can decide wif we want to remove this but for now should still have this field and always set it to 'true'.
-   **Success Response (200 OK):**
    ```json
    {
        "status": "success",
        "images": [
            {
                "jpeg_data": "data:image/jpeg;base64,...",
                "description": "a red sports car",
                "timestamp": 1678886400000,
                "score": 0.954
            }
        ]
    }
    ```
    -   `images` (array of objects): The list of matching images.
        -   `jpeg_data` (string): Base64 encoded JPEG thumbnail data.
        -   `description` (string): The description associated with the image.
        -   `timestamp` (number): The UNIX timestamp (in milliseconds) when the image was captured.
        -   `score` (number): The similarity score of this result.
-   **Error Response (400 Bad Request):** If the request body is malformed or the query fails.

### Notify Thumbnail Click

Informs the server that a user has clicked on a thumbnail, in preparation for video playback.

-   **Endpoint:** `POST /api/video-thumbnail-clicked`
-   **Method:** `POST`
-   **Request Body:**
    ```json
    {
        "session_id": "webrtc-session-id-for-playback",
        "timestamp": 1678886400000,
        "description": "a red sports car"
    }
    ```
    -   `session_id` (string): The WebRTC session ID obtained from `/api/webrtc/session-video-thumbnail`.
    -   `timestamp` (number): The timestamp of the clicked thumbnail.
    -   `description` (string): The description of the clicked thumbnail.
-   **Success Response (200 OK):**
    ```json
    {
        "status": "ok"
    }
    ```
-   **Error Response (400 Bad Request):** If the request failed

---

## 3. WebRTC Streaming API

This set of endpoints manages the WebRTC signaling process for both the main live video feed and the on-demand playback of query results.

**General WebRTC Signaling Flow:**

1.  Client: Request a session ID for the desired stream type (`/api/webrtc/session-*`).
2.  Client: Request an SDP offer from the server (`/api/webrtc/offer`).
3.  Server: Responds with the SDP offer.
4.  Client: Sets the remote description (the offer), creates an answer, and sets its local description.
5.  Client: Sends its SDP answer to the server (`/api/webrtc/answer`).
6.  Client & Server: Exchange ICE candidates as they are discovered (`/api/webrtc/ice-candidate`).
7.  The stream begins.

---

### Create WebRTC Session (Live Stream)

Requests a new session ID for the main live camera feed.

-   **Endpoint:** `POST /api/webrtc/session-live-main`
-   **Method:** `POST`
-   **Request Body:** None
-   **Success Response (200 OK):**
    ```json
    {
        "status": "success",
        "session_id": "unique-session-id-for-live-stream"
    }
    ```
-   **Error Response (500 Invalid):** If request is invalid
-   **Error Response (400 Internal Server Error):** If internal extension module service is not found

### Create WebRTC Session (Video Playback)

Requests a new session ID for playing back a video clip from a query result.

-   **Endpoint:** `POST /api/webrtc/session-video-thumbnail`
-   **Method:** `POST`
-   **Request Body:** None
-   **Success Response (200 OK):**
    ```json
    {
        "status": "success",
        "session_id": "unique-session-id-for-playback"
    }
    ```
-   **Error Response (500 Invalid):** If request is invalid
-   **Error Response (400 Internal Server Error):** If internal extension module service is not found

### Get WebRTC Offer

Client requests an SDP offer from the server to initiate a connection.

-   **Endpoint:** `POST /api/webrtc/offer`
-   **Method:** `POST`
-   **Request Body:**
    ```json
    {
        "session_id": "the-session-id-from-above"
    }
    ```
-   **Success Response (200 OK):** A standard WebRTC SDP offer.
    ```json
    {
        "type": "offer",
        "sdp": "v=0\r\no=- ...\r\n"
    }
    ```
-   **Error Response (500 Invalid):** If request is invalid
-   **Error Response (400 Internal Server Error):** If internal extension module service is not found

### Send WebRTC Answer

Client sends its SDP answer back to the server.

-   **Endpoint:** `POST /api/webrtc/answer`
-   **Method:** `POST`
-   **Request Body:** A standard WebRTC SDP answer.
    ```json
    {
        "session_id": "the-session-id",
        "type": "answer",
        "sdp": "v=0\r\no=- ...\r\n"
    }
    ```
-   **Success Response (200 OK):**
    -   **Content-Type:** `text/plain`
    -   **Body:** `OK`
-   **Error Response (500 Invalid):** If request is invalid
-   **Error Response (400 Internal Server Error):** If internal extension module service is not found

### Send ICE Candidate

Client sends a discovered ICE candidate to the server.

-   **Endpoint:** `POST /api/webrtc/ice-candidate`
-   **Method:** `POST`
-   **Request Body:**
    ```json
    {
        "session_id": "the-session-id",
        "candidate": "candidate:...",
        "sdpMid": "0",
        "sdpMLineIndex": 0
    }
    ```
-   **Success Response (200 OK):**
    -   **Content-Type:** `text/plain`
    -   **Body:** `OK`
-   **Error Response (500 Invalid):** If request is invalid
-   **Error Response (400 Internal Server Error):** If internal extension module service is not found

### Get WebRTC Connection Status

Periodically polled by the client to check if the server-side peer connection is still active.

-   **Endpoint:** `GET /api/webrtc/status`
-   **Method:** `GET`
-   **Request Body:** None
-   **Success Response (200 OK):**
    ```json
    {
        "connected": true
    }
    ```
    -   `connected` (boolean): `true` if the server's peer connection is active, `false` otherwise.

### Stop Video Playback Stream

Explicitly tells the server to stop the video playback stream and close the associated peer connection.

-   **Endpoint:** `POST /api/webrtc/video-thumbnail-stop`
-   **Method:** `POST`
-   **Request Body:**
    ```json
    {
        "session_id": "the-session-id-for-playback"
    }
    ```
-   **Success Response (200 OK):**
    -   **Content-Type:** `text/plain`
    -   **Body:** `Connection closed`
-   **Error Response (400 Not Found):** The session id for the connection not found

---

## 4. Storage API

### Get Storage Status

Provides a detailed breakdown of disk space usage for different components of the application.

-   **Endpoint:** `GET /api/storage/status`
-   **Method:** `GET`
-   **Request Body:** None
-   **Success Response (200 OK):**
    ```json
    {
        "total_space": 1000000000,
        "available_space": 20000000,
        "used_space": 80000000,
        "breakdown": {
            "database": 5000000,
            "faissdb": 100000000,
            "thumbnail": 30000000,
            "video": 7000000000
        },
        "timestamp": 1678886400000,
        "status": "success"
    }
    ```
    -   All storage space field values are in bytes.
    -   `breakdown` (object): Contains the size of individual data directories in bytes
    -   `timestamp` (number): current epoch time stamp in seconds
-   **Error Response (When data is unavailable):**
    ```json
    {
        "total_space": "N/A",
        "available_space": "N/A",
        "used_space": "N/A",
        "breakdown": {
            "database": "N/A",
            "faissdb": "N/A",
            "thumbnail": "N/A",
            "video": "N/A"
        },
        "status": "error",
        "message": "Failed to retrieve storage info"
    }
    ```
