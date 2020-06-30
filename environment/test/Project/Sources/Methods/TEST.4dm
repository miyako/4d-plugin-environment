//%attributes = {}
$up:=Expand environment string ("%userprofile%")
$path:=Get environment variable ("BAZEL_VC")

$cn:=Expand environment string ("%SystemRoot%")

$path:=Get environment variable ("PATH")

PUT ENVIRONMENT VARIABLE ("TEST";Generate UUID:C1066)

$test:=Get environment variable ("TEST")