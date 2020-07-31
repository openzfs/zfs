#cert stores
#cert:\localmachine\my
#Cert:\CurrentUser\My

#config
$plaintextpwd = 'password1234'
$subject = "OpenZFS Test Signing Certificate"
$filename = 'test_sign_cert'
#$dirname = 'c:\'
$dirname = ''
$yearsvalid = 3

#generate
$date_now = Get-Date
$extended_date = $date_now.AddYears($yearsvalid)
$cert = New-SelfSignedCertificate -CertStoreLocation Cert:\CurrentUser\My -Type CodeSigningCert -Subject $subject -notafter $extended_date

#export with password
$filepathpass = $dirname + $filename + '_pass.pfx'
$pwd = ConvertTo-SecureString -String $plaintextpwd -Force -AsPlainText
$path = 'cert:\CurrentUser\My\' + $cert.thumbprint
Export-PfxCertificate -cert $path -FilePath $filepathpass -Password $pwd

#export "without" password
$filepathnopass = $dirname + $filename + '_nopass.pfx'
$passin = 'pass:' + $plaintextpwd
&"C:\Program Files\OpenSSL-Win64\bin\openssl.exe"  pkcs12 -in $filepathpass -nodes -noenc -passin $passin | &"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" pkcs12 -export -keypbe NONE -certpbe NONE  -noenc -nomaciter -noiter -nomac -passout pass: -out $filepathnopass

