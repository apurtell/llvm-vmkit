package j3mgr;

import j3.J3Mgr;

import org.osgi.framework.Bundle;
import org.osgi.framework.BundleContext;
import org.osgi.framework.BundleEvent;
import org.osgi.framework.BundleListener;
import org.osgi.framework.ServiceReference;
import org.osgi.service.packageadmin.PackageAdmin;

public class J3MgrImpl
	implements J3Mgr, BundleListener
{
	BundleContext context;
	
	public void open(BundleContext bundleContext)
	{
		context = bundleContext;
		
		context.addBundleListener(this);
	}

	public void close()
	{
		context.removeBundleListener(this);
		context = null;
	}

	public void setBundleStaleReferenceCorrected(long bundleID, boolean corrected) throws Exception
	{
		j3.vm.OSGi.setBundleStaleReferenceCorrected(bundleID, corrected);
		
		if (corrected && (context.getBundle(bundleID) == null)) {
			j3.vm.OSGi.notifyBundleUninstalled(bundleID);
			refreshFramework();
		}
	}

	public boolean isBundleStaleReferenceCorrected(long bundleID) throws Exception
	{
		return j3.vm.OSGi.isBundleStaleReferenceCorrected(bundleID);
	}

	public void bundleChanged(BundleEvent event)
	{
		if (event.getType() != BundleEvent.UNINSTALLED) return;
		j3.vm.OSGi.notifyBundleUninstalled(event.getBundle().getBundleId());
		refreshFramework();
	}
	
	void refreshFramework()
	{
		ServiceReference<?> pkgAdminRef = (ServiceReference<?>)context.getServiceReference(
			"org.osgi.service.packageadmin.PackageAdmin");
		PackageAdmin pkgAdmin = (PackageAdmin)context.getService(pkgAdminRef);
		pkgAdmin.refreshPackages(null);
		context.ungetService(pkgAdminRef);
	}
	
	// THE FOLLOWING METHODS ARE DEBUGGING HELPERS
	// THEY SHOULD BE REMOVED IN PRODUCTION
	
	public void setBundleStaleReferenceCorrected(String bundleNameOrID, String corrected) throws Exception
	{
		long bundleID = getBundleID(bundleNameOrID);
		setBundleStaleReferenceCorrected(bundleID, corrected.equals("yes"));
	}
	
	public void isBundleStaleReferenceCorrected(String bundleNameOrID) throws Exception
	{
		long bundleID = getBundleID(bundleNameOrID);
		boolean value = isBundleStaleReferenceCorrected(bundleID);
		System.out.println("isBundleStaleReferenceCorrected(bundleID=" + bundleID + ") = " + (value ? "yes" : "no"));
	}

	long getBundleID(String symbolicNameOrID)
	{
		try {
			long bundleID = Long.parseLong(symbolicNameOrID);
			
			if (context.getBundle(bundleID) == null)
				System.out.println("WARNING: bundleID=" + bundleID + " is invalid.");
			
			return (bundleID < 0) ? -1 : bundleID;
		} catch (NumberFormatException e) {
			// This is not a bundle ID, it must be a symbolic name
		}
		
		Bundle[] bundles = context.getBundles();
		for (int i=0; i < bundles.length; ++i) {
			if (symbolicNameOrID.equals(bundles[i].getSymbolicName()))
				return bundles[i].getBundleId();
		}
		return -1;
	}

	public void dumpClassLoaderBundles()
	{
		j3.vm.OSGi.dumpClassLoaderBundles();
	}
}
