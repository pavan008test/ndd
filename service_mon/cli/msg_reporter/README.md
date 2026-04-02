# Message Reporter CLI

This is a command-line interface (CLI) tool for sending messages to a service. The tool supports two types of messages: `critical_info` and `health_info`.

## Prerequisites

1. Ensure the required dependencies are installed and available in your environment.
2. Build the project using the provided `Makefile`:
   ```bash
   make clean
   make
   ```

## Usage

The CLI expects the following arguments:

```bash
./msg_reporter <message_type> <service_name> [additional_arguments]
```

### Arguments

1. **`message_type`**: The type of message to send. Supported values are:
   - `critical_info`
   - `health_info`

2. **`service_name`**: The name of the service to which the message will be sent.

3. **Additional Arguments**:
   - For `critical_info`: Requires two additional arguments:
     - `<error_code>`: An integer representing the error code.
     - `<message>`: The message content.
   - For `health_info`: Requires one additional argument:
     - `<message>`: The message content.


### Sending a `critical_info` Message

```bash
./msg_reporter critical_info <service_name> <err_code> <msg> <aux_code>
```


#### Example
```bash
./msg_reporter critical_info MyService 1001 "Critical error occurred" 300
```

- **`critical_info`**: Message type.
- **`MyService`**: Service name.
- **`1001`**: Error code.
- **`"Critical error occurred"`**: Message content.
- **`1001`**: Aux code.

### Sending a `health_info` Message

```bash
./msg_reporter health_info <service_nam> <stringified_json_data>
```

#### Example

```bash
./msg_reporter health_info MyService '"{\"health_info:accessory\":{\"id\": \"001462300029\", \"SN\": \"001462300029\",\"PN\": \"030000715030\"}}"'
```

- **`health_info`**: Message type.
- **`MyService`**: Service name.
- **`'"{\"health_info:accessory\":{\"id\": \"001462300029\", \"SN\": \"001462300029\",\"PN\": \"030000715030\"}}"'`**: Stringified data content.


## Exit Codes

The program returns specific exit codes to indicate the result of the operation:

| Exit Code | Description                                     |
|-----------|-------------------------------------------------|
| `0`       | Success                                         |
| `1`       | No message type provided                        |
| `2`       | Invalid number of arguments for `critical_info` |
| `3`       | Error converting error code                     |
| `4`       | Failed to send `critical_info` message          |
| `5`       | Invalid number of arguments for `health_info`   |
| `6`       | Failed to send `health_info` message            |
| `7`       | Invalid message type                            |
| `8`       | Exception occurred                              |

## Notes

- Ensure the service specified in `<service_name>` is configured and running.
- The `message` content will be trimmed of leading and trailing whitespace before being sent.
- For `critical_info`, the `<error_code>` must be a valid integer.

## License

This tool is proprietary and confidential. Unauthorized copying or distribution is strictly prohibited.