// The Digital Grove Codebase
// Copyright (c) 2024 Ryan Fleury. All rights reserved.

////////////////////////////////////////////////////////////////
//~ rjf: Basic Type Functions

internal HTTP_StatusKind HTTP_StatusKindFromCode(HTTP_StatusCode code) {
  HTTP_StatusKind kind = HTTP_StatusKind_Null;
  if (0) {
  } else if (HTTP_StatusCode_FirstInformational <= code &&
             code <= HTTP_StatusCode_LastInformational) {
    kind = HTTP_StatusKind_Informational;
  } else if (HTTP_StatusCode_FirstSuccessful <= code &&
             code <= HTTP_StatusCode_LastSuccessful) {
    kind = HTTP_StatusKind_Successful;
  } else if (HTTP_StatusCode_FirstRedirection <= code &&
             code <= HTTP_StatusCode_LastRedirection) {
    kind = HTTP_StatusKind_Redirection;
  } else if (HTTP_StatusCode_FirstClientError <= code &&
             code <= HTTP_StatusCode_LastClientError) {
    kind = HTTP_StatusKind_ClientError;
  } else if (HTTP_StatusCode_FirstServerError <= code &&
             code <= HTTP_StatusCode_LastServerError) {
    kind = HTTP_StatusKind_ServerError;
  }
  return kind;
}
