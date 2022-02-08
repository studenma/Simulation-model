/*
 * IMS Projekt 2017/18
 * 3. Výroba másla v ČR
 *
 * Prostudujte okolnosti  produkce  mléka, dodavatelského a zpracovatelského  řetězce v mlékarenství a
 * modelujte výrobu másla  v podmínkách ČR. Vycházejte z běžně dostupných informací o produkci mléka a
 * zpracovatelích mléka. Téma  je volné, včetně  metod jeho zpracování - studie však musí být založena
 * na vlastním modelu. Lze pojmout buď jako studii produkce mléka pro zpracovatelský průmysl nebo jako
 * studii výroby mléčných produktů (s důrazem na máslo).
 *
 * Autor: Michal Zilvar (xzilva02)
 * Last edit: 05.12.2017
 */

#include <iostream>
#include <vector>
#include <simlib.h>
#include <cstdio>
#include <math.h>
#include <ctime>

#define RUN_ERR	-1

typedef struct {
	int Vyrobeno;
	int Sklad;
	int Zkazeno;
} Stats;

// Globální proměnné, které načítáme z parametru
int POCET_LIDI;
int POCET_KRAV;
int POCET_MINUT;
double PRACOVNI_DOBA;

// Pomocné globální promenne
bool pracovniDoba = false;
bool *podojenaKrava;

// Fronty a linky na dojení krávy
Facility *krava;
Queue FrontaZamestnanci("Zaměstnanci neprovozující činnost");
Queue FrontaMleko("Uskladněné mléko s krustou");
Queue FrontaZamestnanciDoma("Zaměstnanci mimo pracovní dobu");
Queue FrontaSmetana("Smetana na skladě");
Queue FrontaMaslo("Výslednž produkt Máslo");

// Statistiky pro výpis
Stats SKravy;
Stats SMleko;
Stats SSmetana;
Stats SMaslo;

// Potřebné pro výpočet přesčasů
long int CounterTime = 0;
long int Prescasy = 0;
long int PosledniKonecSmenyMin = 0;
double PosledniKonecSmeny = 0.0;
long int OdpracovanoCelkem = 0;

// Histogramy jednotlivých akcí zaměstnanců
TStat histogramDojeni("Proces dojení");
TStat histogramMleko("Proces zrání mléka (tvorba smetany na povrchu)");
TStat histogramSmetana("Proces získání smetany z mléka");
TStat histogramMaslo("Proces stloukání másla");

// Doplňující informační histogramy
TStat histogramUskladneniM("Průměrná doba skladování mléka");
TStat histogramTrvanlivostM("Histogram zkaženého mléka a jeho skladování");
TStat histogramTrvanlivostS("Rychlost využití smetany");
TStat histogramOdpracovano("Průměrná doba činnosti zaměstnance");
TStat histogramPrescasy("Přesčasy nutné k dokončení činnosti");

// Finální produkt, počítáme dobu přípravy a výsledný počet
class Maslo : public Process {
	void Behavior() {

		// Uložíme produkt
		SMaslo.Vyrobeno++;
		SMaslo.Sklad++;

		// Vložíme do fronty a necháváme jej čekat
		FrontaMaslo.Insert(this);
		Passivate();
	}
};

// Meziprodukt smetana, počítáme množství ve frontě a zpracováváme na máslo
class Smetana : public Process {
	void Behavior() {

		// Uložíme produkt
		SSmetana.Vyrobeno++;
		SSmetana.Sklad++;

		// Uložíme čas uskladnění pro výpočet doby uskladnění smetany
		double uskladneni = Time;

		// Uložíme smetanu do chladu a čekáme na zpracování na máslo
		FrontaSmetana.Insert(this);
		Passivate();

		// Vložíme dobu uskladnění smetany ve chladu
		histogramTrvanlivostS(Time - uskladneni);
	}
};

// Meziprodukt mléka, čekáme na zpracování
class Mleko : public Process {
	void Behavior() {

		// Uložíme čas uskladnění pro výpočet doby uskladnění a případné zkažení mléka
		double uskladneni = Time;

		// Přičteme meziprodukt
		SMleko.Sklad++;

		// Necháváme mléku v chladu a vyčkáváme
		FrontaMleko.Insert(this);
		Passivate();

		// Uložíme délku skladování
		histogramUskladneniM(Time - uskladneni);

		// Mléko vydrží maximálně 4-5 dní po získání
		if(Time > (uskladneni+(60*24*3))) {

			// Uložíme histogram zkaženého mléka
			histogramTrvanlivostM(Time - uskladneni);

			// Zahodíme jedno mléko (nestíháme zpracovávat, určitě existuje další uskladněný produkt)
			if(!FrontaMleko.Empty()) FrontaMleko.GetFirst();

			// Rychlá statistika
			SMleko.Zkazeno++;
		}
	}
};

// Proces zrání mléka, nechává se uležet, aby na povrchu vznikla smetana
class ZraniMleka : public Process {
	void Behavior() {

		// Přičteme meziprodukt
		SMleko.Vyrobeno++;

		// Uložíme aktuální čas před započetím zrání
		double tTime = Time;

		// Průměrná doba zrání je 1-2 dny
		RandomSeed(time(NULL));
		Wait(1440 * (1 + Random()));

		// Vzniká zralé mléko připravené na odběr smetany
		(new Mleko)->Activate();

		// Zaevidujeme informace a dobu zrání
		histogramMleko(Time - tTime);

		// Uvolníme zaměstnance, aby mohl zpracovávat
		// V případě, že nestíhají dojit krávy a vyrábět máslo již všichni pracují a žádný se neuvolní
		if(!FrontaZamestnanci.Empty()) {
			FrontaZamestnanci.GetFirst()->Activate();
		}
	}
};

// Hlavní proces zaměstnance, který řídí celou výrobu. Použito goto pro opakující se akce a vyhledávání aktuální práce
class Zamestnanec : public Process {
	void Behavior() {

pracovniPriority:

		// Je-li pracovní doba
		if(pracovniDoba) {

			// Uložíme aktuální čas pro histogramy
			double tTime = Time;
			long int cMinute = CounterTime;

			// Prioritu má dojení. Vždy je nutné krávu podojit
			for(int i = 0; i < POCET_KRAV; i++) {

				// Pokud kráva nebyla dojená v posledních 12ti hodinách a právě ji nikdo nedojí
				if(!podojenaKrava[i] && !krava[i].Busy()) {

					// Obsadíme linku kráva => Probíhá dojení
					Seize(krava[i]);

					// Občas není ten pravý den na dojení a zabere 22 - 40 minut, pokud kráva nekopne a neuteče
					RandomSeed(time(NULL));
					if(Random() < 0.05) {
						RandomSeed(time(NULL));
						Wait(22 + Random()*18);
					}

					// Dojení trvá v průměru 16 - 26 minut, dle zkušeností zaměstnance
					else {
						RandomSeed(time(NULL));
						Wait(16 + Random()*10);
					}

					// Nastavíme příznak dokončení dojení a uvolníme linku. Kráva se jde pást
					podojenaKrava[i] = true;
					Release(krava[i]);

					// Vznikl nám meziprodukt mléko, aktivujeme jej
					(new ZraniMleka)->Activate();

					// Vložíme informace o dojení do histogramu
					histogramOdpracovano(Time - tTime);
					histogramDojeni(Time - tTime);
					OdpracovanoCelkem += (CounterTime - cMinute);

					// Uložíme rychlou statistiku
					SKravy.Vyrobeno++;

					// Vracíme se na počátek a zjišťujeme, zda-li je potřeba něco neodkladně udělat
goto pracovniPriority;
				}
			}

			// Pokud je připravena smetana ke zpracování
			if(!FrontaSmetana.Empty()) {

				// Vyjmeme smetanu z chladu a jdeme stloukat máslo
				FrontaSmetana.GetFirst()->Activate();

				// Vyjmeme ze skladu
				SSmetana.Sklad--;

				// Máslo se ručně stlouká 40-63 minut bez dalších komplikací
				RandomSeed(time(NULL));
				Wait(40 + Random()*23);

				// Vzniká finální produkt másla
				(new Maslo)->Activate();

				// Statistika o stloukání
				histogramOdpracovano(Time - tTime);
				histogramMaslo(Time - tTime);
				OdpracovanoCelkem += (CounterTime - cMinute);

				// Vracíme se na počátek a zjišťujeme, zda-li je potřeba něco neodkladně udělat
goto pracovniPriority;
			}

			// Poslední prioritu má zpracování mléka
			if(!FrontaMleko.Empty()) {

				// Dojdeme pro mléko do chladu a získáváme smetanu
				FrontaMleko.GetFirst()->Activate();

				// Vyjmeme ze skladu
				SMleko.Sklad--;

				// Sběr smetany z mléka zabera zhruba 12-16 minut
				RandomSeed(time(NULL));
				Wait(12 + Random()*4);
				(new Smetana)->Activate();

				// Uložíme do histogramu
				histogramOdpracovano(Time - tTime);
				histogramSmetana(Time - tTime);
				OdpracovanoCelkem += (CounterTime - cMinute);

				// Vracíme se na počátek a zjišťujeme, zda-li je potřeba něco neodkladně udělat
goto pracovniPriority;
			}

			// V tuto chvíli není nic na práci a tak se zaměstnanec věnuje svým činnostem
			FrontaZamestnanci.Insert(this);
			Passivate();

			// Po probuzeníme kontrolujeme potřebné činnosti
goto pracovniPriority;
		}

		// Pracovní doba skončila a zaměstnanec právě dokončil rozdělanou činnost
		else
		{
			// Přičteme počet minut v přesčasu
			Prescasy += (CounterTime - PosledniKonecSmenyMin);

			// Vložíme do histogramu přesné hodnoty
			histogramPrescasy((Time - PosledniKonecSmeny) / 2);

			// Posíláme zaměstnance domů a očekáváme ho na další směně
			FrontaZamestnanciDoma.Insert(this);
			Passivate();

			// Zaměstnanec se vrací do práce a kontroluje všechny úkony dle priority
goto pracovniPriority;
		}
	}
};

// Hlavní časovač pro přepínání pracovní a nepracovní směny
class ZmenaSmeny : public Event {
	void Behavior() {

		// Pokud je zrovna pracovní doba, ukončíme ji
		if(pracovniDoba) {

			// Aktivujeme počátek směny 12 hodin po počátku první směny
			Activate(Time+(720 - (PRACOVNI_DOBA*30)));

			// Posíláme zaměstnance neprovádějící činnost domů
			while(!FrontaZamestnanci.Empty())
				FrontaZamestnanciDoma.Insert(FrontaZamestnanci.GetFirst());

			// Nastavujeme ukazatel pracovní doby
			pracovniDoba = false;

			// Nastavujeme proměnné pro získání přesčasů
			PosledniKonecSmenyMin = CounterTime;
			PosledniKonecSmeny = Time;
		}

		// Zahajujeme pracovní dobu - jedná se o první směnu
		else {

			// Pracovní doba se rozděluje na 2 směny za den, proto trvá polovičku času
			// Krávy je nutné podojit jak ráno, tak i večer
			Activate(Time + (PRACOVNI_DOBA*30));

			// Začátek směny vždy začíná dojením krav, nastavujeme jejich příznak na nepodojenou
			for(int i = 0; i < POCET_KRAV; i++)
				podojenaKrava[i] = false;

			// Oficiální příznak pracovní doby
			pracovniDoba = true;

			// Svoláváme zaměstnance ze svých domovů do práce
			while(!FrontaZamestnanciDoma.Empty())
				FrontaZamestnanciDoma.GetFirst()->Activate();
		}
	}
};

// Dodatečný časovač pro počítání minut
class Casovac : public Event {
	void Behavior() {

		// Aktivujeme jej za další minutu
		Activate(Time + 1);

		// Přičteme minutu v čítači
		CounterTime++;
	}
};

// Funkce na získání parametrů
bool validArgs(int argc, char **argv, std::string *output, std::string *outputEff) {

	// Vždy spouštíme s pěti parametry
	if(argc != 7) return false;

	// Pokud se nám nepodaří načíst číselné údaje, jedná se o chybu
	if(	std::sscanf(argv[1], "%d", &POCET_KRAV) <= 0 ||					// Počet krav je číselný údaj o množství krav v mlékárně
		std::sscanf(argv[2], "%d", &POCET_LIDI) <= 0 ||					// Počet lidí je množství zaměstnanců
		std::sscanf(argv[3], "%lf", &PRACOVNI_DOBA) <= 0 ||				// Pracovní doba je množství hodin odpracovaných za den (2 směny denně)
		std::sscanf(argv[4], "%d", &POCET_MINUT) <= 0) return false;	// Počet hodin, jak dlouho simulujeme běh farmy

	// Uložíme název souboru pro výstup
	*output += argv[5];
	*outputEff += argv[6];

	// Dny převedeme na minuty, které jsou hlavním modelovým časem
	POCET_MINUT *= 60*24;

	// Zpracování proběhlo v pořádku
	return true;
}

// Zarovnáme číslo do řetězce vpravo
std::string calculateToField(int n, int len) {

	// Vložíme proměnnou do stringu
	std::string str = std::to_string(n);

	// Dokud je kratší než len znaků, odsadíme doleva
	while(str.length() < len)
		str = " " + str;

	// Vracíme string
	return str;
}

// Zarovnáme číslo do řetězce vpravo
std::string calculateToFieldD(double n) {

	// Vložíme proměnnou do stringu
	std::string str = std::to_string(n) + "%";

	// Dokud je kratší než 12 znaků, odsadíme doleva
	while(str.length() < 12)
		str += " ";

	// Vracíme string
	return str;
}

// Vynulujeme statistiky
void nullStats(Stats *S) {
	S->Vyrobeno = 0;
	S->Sklad = 0;
	S->Zkazeno = 0;
}

// Koeficient zatížení zaměstnanců
double emplEff() {

	// Efektivita zaměstnanců, zda se flákají, nebo ne
	return (OdpracovanoCelkem / (POCET_LIDI * PRACOVNI_DOBA * POCET_MINUT / 24.0));
}

// Koeficient efektivity
double calcEfficiency(double employeeEff) {

	// Přesčasy jsou neefektivním systémem, proto koeficient převrátíme
	if(employeeEff >= 1.0) employeeEff = 1.0 / employeeEff;

	// Ve vzorci zvážíme množství vyrobených produktů, ku těm zkaženým a vynásobíme efektivitou zaměstnanců
	return ((((SKravy.Vyrobeno + SMleko.Vyrobeno) / 2.0)*(1.0 - (1.0 * SMleko.Zkazeno / SMleko.Vyrobeno))) / SKravy.Vyrobeno) * employeeEff;
}

// Comment to the result
std::string simulationResult(double employeeEff, double efficiency) {
	std::string tmp;

	// Víc jak 2% odpadu
	if(SMleko.Zkazeno > SMleko.Vyrobeno*0.02) tmp = "Zaměstnanci nestíhají zpracovávat mléko.";

	// Přetížení zaměstnanců
	else if(employeeEff > 1.02) tmp = "Zaměstnanci jsou přetížení.";

	// Neefektivní
	else if(employeeEff <= 0.85) tmp = "Evidujeme přebytek zaměstnanců.";

	// Průměrná efektivita
	else if(efficiency <= 0.975) tmp = "Domácí mlékárna takto může fungovat.";

	// Seriózní výsledek
	else tmp = "Docílili jsme ideálního nastavení.";

	return tmp;
}

// Výchozí spouštěcí funkce
int main(int argc, char **argv) {

	// Řetězec pro výstupní soubor
	std::string output;
	std::string outputEff;

	// Pokusíme se získat všechny parametry
	if(!validArgs(argc, argv, &output, &outputEff)) {

		// Parametry jsou nevalidní, vypisujeme chybu
		fprintf(stderr, "Použití: %s <počet krav> <počet lidí> <pracovní doba v hodinách> <počet dní> <output> <eff>\n", argv[0]);
		
		// Končíme s chybou
		return RUN_ERR;
	}

	// Vynulujeme interní statistiku
	nullStats(&SKravy);
	nullStats(&SMleko);
	nullStats(&SSmetana);
	nullStats(&SMaslo);

	// Deklarujeme proměnnou a linku dle zadaného množství krav
	podojenaKrava = new bool[POCET_KRAV];
	krava = new Facility[POCET_KRAV];

	// Na počátku všeho vytvoříme zaměstnance
	for(int i = 0; i < POCET_LIDI; i++)
		FrontaZamestnanciDoma.Insert(new Zamestnanec);

	// Vypíšeme název simulace
	Print("\n   +----------------------------------------------+\n");
	Print(  "   |        Tradiční domácí ruční mlékárna        |\n");
	Print(  "   +----------------------------------------------+\n");

	// Nastavíme výstup histogramů
	SetOutput(output.c_str());

	// Nastavíme časovou osu simulace
	Init(0, POCET_MINUT);

	// Aktivujeme hlavní eventy pro změnu směny a časovač na přesčasy
	(new ZmenaSmeny)->Activate();
	(new Casovac)->Activate();

	// Spustíme simulaci
	Run();

	// Vypíšeme histogramy
	SIMLIB_statistics.Output();

	histogramOdpracovano.Output();
	histogramPrescasy.Output();

	histogramDojeni.Output();
	histogramMleko.Output();
	histogramSmetana.Output();
	histogramMaslo.Output();
	histogramTrvanlivostM.Output();
	histogramTrvanlivostS.Output();

	FrontaZamestnanci.Output();
	FrontaMleko.Output();
	FrontaZamestnanciDoma.Output();
	FrontaSmetana.Output();
	FrontaMaslo.Output();

	for(int i = 0; i < POCET_KRAV; i++)
		krava[i].Output();

	// Získáme hodnoty efektivity
	double employeeEff = emplEff();
	double efficiency = calcEfficiency(employeeEff);

	// Rychlé shrnutí výstupu
	printf( "   | Celkem podojeno krav       | %s |\n", calculateToField(SKravy.Vyrobeno, 15).c_str());
	printf( "   | Mléko                      |                 |\n");
	printf( "   |  - celkem vyrobeno         | %s |\n", calculateToField(SMleko.Vyrobeno, 15).c_str());
	printf( "   |  - zůstatek na skladě      | %s |\n", calculateToField(SMleko.Sklad, 15).c_str());
	printf( "   |  - z toho se zkazilo       | %s |\n", calculateToField(SMleko.Zkazeno, 15).c_str());
	printf( "   | Smetana                    |                 |\n");
	printf( "   |  - celkem vyrobeno         | %s |\n", calculateToField(SSmetana.Vyrobeno, 15).c_str());
	printf( "   |  - zůstatek na skladě      | %s |\n", calculateToField(SSmetana.Sklad, 15).c_str());
	printf( "   | Máslo                      |                 |\n");
	printf( "   |  - celkem vyrobeno         | %s |\n", calculateToField(SMaslo.Vyrobeno, 15).c_str());
	printf( "   +----------------------------------------------+\n");
	printf( "   | Celkem odpracováno         | %s hod. |\n", calculateToField(OdpracovanoCelkem/60, 10).c_str());
	printf( "   | Celkové přesčasy           | %s hod. |\n", calculateToField(Prescasy/60, 10).c_str());
	printf( "   | Průměrný přesčas na směnu  | %s min. |\n", calculateToField(Prescasy/POCET_LIDI/((POCET_MINUT/60/24))/2, 10).c_str());
	printf( "   +----------------------------------------------+\n");
	printf( "   | Zatížení zaměstnanců:           %s |\n", calculateToFieldD(employeeEff*100).c_str());
	printf( "   | Celková efektivita systému:     %s |\n", calculateToFieldD(efficiency*100).c_str());
	printf( "   +----------------------------------------------+\n");
	printf( "     %s\n\n", simulationResult(employeeEff, efficiency).c_str());

	// Zapíšeme výslednou efektivitu
	FILE *f = fopen(outputEff.c_str(), "a");
	fprintf(f, "%g\n", efficiency*100);
	fclose(f);

	// Pro výpočet průměrné efektivity
	double hodnota;
	double prumer = 0.0;
	int nacteno = 0;

	// Otevřeme soubor pro načtení původních hodnot
    char * line = NULL;
    size_t len = 0;
    ssize_t read;
	f = fopen(outputEff.c_str(), "r");
	while ((read = getline(&line, &len, f)) != -1) {
        std::sscanf(line, "%lf\n", &hodnota);
        prumer += hodnota;
        nacteno++;
    }

    // Vypočítáme průměr a vložíme na výstup
    prumer = prumer/nacteno;
    printf( "     Průměrná efektivita: %g\n", prumer);
	return 0;
}