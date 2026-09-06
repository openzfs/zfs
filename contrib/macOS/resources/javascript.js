const __IC_FLAT_DISTRIBUTION__=false;
const IC_OS_DISTRIBUTION_TYPE_ANY=0;
const IC_OS_DISTRIBUTION_TYPE_CLIENT=1;
const IC_DISK_TYPE_DESTINATION=0;
const IC_OS_DISTRIBUTION_TYPE_SERVER=2;
const IC_DISK_TYPE_STARTUP_DISK=1;

const IC_COMPARATOR_IS_EQUAL=0;
const IC_COMPARATOR_IS_GREATER=1;
const IC_COMPARATOR_IS_NOT_EQUAL=2;
const IC_COMPARATOR_IS_LESS=-1;

function IC_CheckOS(inDiskType,inMustBeInstalled,inMinimumVersion,inMaximumVersion,inDistributionType)
{
    var tOSVersion=undefined;

    /* Check Version Constraints */

    if (inDiskType==IC_DISK_TYPE_DESTINATION)
    {
	if (my.target.systemVersion!=undefined)
	{
	    tOSVersion=my.target.systemVersion.ProductVersion;
	}

        /* Check if no OS is installed on the potential target */

        if (tOSVersion==undefined)
	{
	    return (inMustBeInstalled==false);
	}

        if (inMustBeInstalled==false)
	{
	    return false;
	}
    }
    else
    {
	tOSVersion=system.version.ProductVersion;
    }

    if (system.compareVersions(tOSVersion,inMinimumVersion)==-1)
	return false;

    if (inMaximumVersion!=undefined &amp;&amp;
	system.compareVersions(tOSVersion,inMaximumVersion)==1)
	return false;

    /* Check Distribution Type */

    if (inDistributionType!=IC_OS_DISTRIBUTION_TYPE_ANY)
    {
	var tIsServer;

	if (system.compareVersions(tOSVersion,'10.8.0')==-1)
	{
	    if (inDiskType==IC_DISK_TYPE_DESTINATION)
	    {
		tIsServer=system.files.fileExistsAtPath(my.target.mountpoint+'/System/Library/CoreServices/ServerVersion.plist');
	    }
	    else
	    {
		tIsServer=system.files.fileExistsAtPath('/System/Library/CoreServices/ServerVersion.plist');
	    }
	}
	else
	{
	    if (inDiskType==IC_DISK_TYPE_DESTINATION)
	    {
		tIsServer=system.files.fileExistsAtPath(my.target.mountpoint+'/Applications/Server.app');
	    }
	    else
	    {
		tIsServer=system.files.fileExistsAtPath('/Applications/Server.app');
	    }
	}
	if (inDistributionType==IC_OS_DISTRIBUTION_TYPE_CLIENT &amp;&amp; tIsServer==true)
	{
	    return false;
	}

	if (inDistributionType==IC_OS_DISTRIBUTION_TYPE_SERVER &amp;&amp; tIsServer==false)
	{
	    return false;
	}
    }

    return true;
}

function IC_CheckScriptReturnValue(inScriptPath,inArguments,inComparator,inReturnValue)
{
    var tReturnValue;

    if (inScriptPath.charAt(0)=='/')
    {
	/* Check Absolute Path Existence */

	if (system.files.fileExistsAtPath(inScriptPath)==false)
	{
	    return false;
	}
    }
    else
    {
	if (__IC_FLAT_DISTRIBUTION__==true &amp;&amp; system.compareVersions(system.version.ProductVersion, '10.6.0')&lt;0)
	{
	    system.log("[WARNING] Embedded scripts are not supported in Flat distribution format on Mac OS X 10.5");

	    return true;
	}
    }
    if (inArguments.length&gt;0)
    {
	var tMethodCall;
	var tStringArguments=[];

	for(var i=0;i&lt;inArguments.length;i++)
	{
	    tStringArguments[i]='inArguments['+i+']';
	}

	tMethodCall='system.run(inScriptPath,'+tStringArguments.join(',')+');';

	tReturnValue=eval(tMethodCall);
    }
    else
    {
	tReturnValue=system.run(inScriptPath);
    }

    if (tReturnValue==undefined)
    {
	return false;
    }

    if (inComparator==IC_COMPARATOR_IS_EQUAL)
    {
	return (tReturnValue==inReturnValue);
    }
    else if (inComparator==IC_COMPARATOR_IS_GREATER)
    {
	return (tReturnValue&gt;inReturnValue);
    }
    else if (inComparator==IC_COMPARATOR_IS_LESS)
    {
	return (tReturnValue&lt;inReturnValue);
    }
    else if (inComparator==IC_COMPARATOR_IS_NOT_EQUAL)
    {
	return (tReturnValue!=inReturnValue);
    }

    return false;
}

function installation_check()
{
    var tResult;

    tResult=IC_CheckOS(IC_DISK_TYPE_STARTUP_DISK,true,'10.5',undefined,IC_OS_DISTRIBUTION_TYPE_ANY);

    if (tResult==false)
    {
	my.result.title = system.localizedStandardStringWithFormat('InstallationCheckError', system.localizedString('DISTRIBUTION_TITLE'));
	my.result.message = ' ';
	my.result.type = 'Fatal';
    }

    if (tResult==true)
    {
	var tScriptArguments1=new Array();

	tResult=IC_CheckScriptReturnValue('poolcheck.sh',tScriptArguments1,IC_COMPARATOR_IS_EQUAL,1);

	if (tResult==false)
	{
	    my.result.title = system.localizedString('REQUIREMENT_FAILED_MESSAGE_INSTALLATION_CHECK_1');
	    my.result.message = system.localizedString('REQUIREMENT_FAILED_DESCRIPTION_INSTALLATION_CHECK_1');
	    my.result.type = 'Fatal';
	}

	if (tResult==true)
	{
	    var tScriptArguments2=new Array();

	    tResult=IC_CheckScriptReturnValue('zevocheck.sh',tScriptArguments2,IC_COMPARATOR_IS_EQUAL,1);

	    if (tResult==false)
	    {
		my.result.title = system.localizedString('REQUIREMENT_FAILED_MESSAGE_INSTALLATION_CHECK_2');
		my.result.message = system.localizedString('REQUIREMENT_FAILED_DESCRIPTION_INSTALLATION_CHECK_2');
		my.result.type = 'Fatal';
	    }
	}
    }

    return tResult;
}
