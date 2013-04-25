<?php
include("root.php");
include($ROOT."common.php");
preambule("Publications - Bibtex", "publi");
?>
<p>
<a name="vee/10/geoffray/vmkit"></a><pre>
@inproceedings{<a href="Publications.php#vee/10/geoffray/vmkit">vee/10/geoffray/vmkit</a>,
  author = {Geoffray, Nicolas and Thomas, Ga\"el and Lawall, Julia and Muller, Gilles and Folliot, Bertil},
  title = {{VMKit: a substrate for managed runtime environments}},
  booktitle = {Proceedings of the international conference on Virtual Execution Environments, VEE~'10},
  publisher = {ACM},
  pdf = {geoffray10vee-vmkit.pdf},
  year = {2010},
  address = {Pittsburgh, PA, USA},
  pages = {51--62},
  abstract = {
Managed Runtime Environments (MREs), such as the JVM and the CLI, form
an attractive environment for program execution, by providing
portability and safety, via the use of a bytecode language and
automatic memory management, as well as good performance, via
just-in-time (JIT) compilation. Nevertheless, developing a fully
featured MRE, including e.g. a garbage collector and JIT compiler, is
a herculean task. As a result, new languages cannot easily take
advantage of the benefits of MREs, and it is difficult to experiment
with extensions of existing MRE based languages.

This paper describes and evaluates VMKit, a first attempt to build a
common substrate that eases the development of high-level MREs. We
have successfully used VMKit to build two MREs: a Java Virtual Machine
and a Common Language Runtime. We provide an extensive study of the
lessons learned in developing this infrastructure, and assess the ease
of implementing new MREs or MRE extensions and the resulting
performance. In particular, it took one of the authors only one month
to develop a Common Language Runtime using VMKit. VMKit furthermore
has performance comparable to the well established open source MREs
Cacao, Apache Harmony and Mono, and is 1.2 to 3 times slower than
JikesRVM on most of the DaCapo benchmarks.
  }
}
</pre>

<a name="dsn/09/geoffray/ijvm"></a><pre>
@inproceedings{<a href="Publications.php#dsn/09/geoffray/ijvm">dsn/09/geoffray/ijvm</a>,
  author = {Geoffray, Nicolas and Thomas, Ga\"el and Muller, Gilles and Parrend, Pierre and Fr\'enot, St\'ephane and Folliot, Bertil},
  title = {{I-JVM: a Java virtual machine for component isolation in OSGi}},
  booktitle = {Proceedings of the international conference on Dependable Systems and Networks, DSN~'09},
  publisher = {IEEE Computer Society},
  pdf = {geoffray09dsn-ijvm.pdf},
  year = {2009},
  address = {Estoril, Portugal},
  pages = {544--553},
  abstract = {
The OSGi framework is a Java-based, centralized, component oriented
platform. It is being widely adopted as an execution environment for
the development of extensible applications. However, current Java
Virtual Machines are unable to isolate components from each other. For
instance, a malicious component can freeze the complete platform by
allocating too much memory or alter the behavior of other components
by modifying shared variables.  This paper presents I-JVM, a Java
Virtual Machine that provides a lightweight approach to isolation
while preserving compatibility with legacy OSGi applications. Our
evaluation of I-JVM shows that it solves the 8 known OSGi
vulnerabilities that are due to the Java Virtual Machine and that the
overhead of I-JVM compared to the JVM on which it is based is below
20\%.
  }
}
</pre>

<a name="pppj/08/geoffray/ladyvm"></a><pre>
@inproceedings{<a href="Publications.php#pppj/08/geoffray/ladyvm">pppj/08/geoffray/ladyvm</a>,
  author = { Geoffray, Nicolas and Thomas, Ga\"el and Cl\'ement, Charles and Folliot, Bertil},
  title = {{A lazy developer approach: building a JVM with third party software}},
  booktitle = {Proceedings of the international symposium on Principles and Practice of Programming in Java, PPPJ~'08},
  year = {2008},
  pages = {73--82},
  address = {Modena, Italy},
  publisher = {ACM},
  pdf = {geoffray08pppj-ladyvm.pdf},
  abstract = {
The development of a complete Java Virtual Machine (JVM)
implementation is a tedious process which involves knowledge in 
different areas: garbage collection, just in time compilation, 
interpretation, file parsing, data structures, etc.
The result is that developing its own virtual machine requires a 
considerable amount of man/year. In this paper
we show that one can implement a JVM with third party
software and with performance comparable to industrial and
top open-source JVMs. Our proof-of-concept implementation 
uses existing versions of a garbage collector, a just in
time compiler, and the base library, and is robust enough to
execute complex Java applications such as the OSGi Felix
implementation and the Tomcat servlet container.  
}
}
</pre>


<?php epilogue() ?>
