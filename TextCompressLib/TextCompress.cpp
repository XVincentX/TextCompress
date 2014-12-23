#pragma once
#define TEXTCOMPRESS_EXPORT
#include <Windows.h>
#include <cmath>
#include <vector>
#include <map>
#include <algorithm> 
#include <utility>
#include <functional>
#include "TextCompress.h"
#include <boost/dynamic_bitset.hpp>

static int X_V;
static unsigned int MinCodeSize;	//Questa variabile conterrà il numero di bit che saranno utilizzati dalla codifica compressa

template<unsigned int chunkByteSize>
class TextCompressor : public ITextCompressor	
{
public:
#pragma region Structures

	typedef struct 		// Definiamo la struttura Element come sequenza di 8*chunkByteSize bit consecutivi. E' la struttura destinata a contenere le parole-codice nella codifica base: quelli che consideriamo i caratteri del documento da comprimere
	{ 
		UINT8 value[chunkByteSize];
	} Element;


	typedef	struct ArrayElement_ {			// Definiamo la struttura ArrayElement, destinata a contenere un array di oggetti Element. Se consideriamo che gli Element sono i caratteri della codifica base, allora ArrayElement è equivalente ad una stringa di caratteri della codifica base
		ArrayElement_() : pElement(0){}		// Costruttore senza parametri della struttura
		ArrayElement_(unsigned int Ws, Element *pElement) : WindowSize(Ws)	// Costruttore della struttura. Instanzia un ArrayElement che contiene Ws Element consecutivi, di cui il primo è dato in ingresso.
		{
			this->pElement = new Element[Ws];
			memcpy(this->pElement,pElement, sizeof(Element) * Ws);
		}
		ArrayElement_(unsigned int Ws) : WindowSize(Ws)		// Costruttore della struttura. Instanzia un ArrayElement vuoto, ovvero una stringa di Ws Element '/0'
		{
			this->pElement = new Element[Ws];;
			memset(this->pElement,0, sizeof(Element) * Ws);
		}
		ArrayElement_(const ArrayElement_ &cp)		// Costruttore della struttura. Instanzia un ArrayElement uguale ad un altro ArrayElement dato in ingresso
		{
			this->WindowSize = cp.WindowSize;
			this->pElement = new Element[cp.WindowSize];
			memcpy(this->pElement,cp.pElement, sizeof(Element) * cp.WindowSize);
		}

		unsigned int WindowSize;	// Indica da quanti Element è costituito ArrayElement, ovvero da quanti caratteri è costituita la stringa. Praticamente è la dimensione, in Element, di ArrayElement
		Element *pElement; 	// Element we are talking about
		~ArrayElement_()	// Distruttore della struttura
		{
			if (pElement != 0)
			{
				delete[] pElement;
				pElement = 0;
			}
		}

		bool operator== (const ArrayElement_ &second) const	// Ridefinizione dell'operatore di uguaglianza. Verifica che due ArrayElement siano della stessa dimensione ed in seguito che contengano gli stessi Element
		{
			if (second.WindowSize != this->WindowSize)
				return false;

			return (memcmp(this->pElement,second.pElement,this->WindowSize) == 0); // Sfrutta la funzione memcmp che confronta due aree di memoria date in ingresso e restituisce '0' se esse sono uguali.
		}

		bool operator!= (const ArrayElement_ &second) const	// Ridefinizione dell'operatore di disuguaglianza. Invoca l'operatore di uguaglianza e restituisce il suo valore negato.
		{
			bool val =  this->operator==(second);
			return !val;
		}

		friend bool operator> (const ArrayElement_ &first, const ArrayElement_ &second)		// Ridefinizione dell'operatore >
		{
			if (first.WindowSize != second.WindowSize)
				return first.WindowSize > second.WindowSize;


			bool b2 = memcmp(first.pElement,second.pElement,first.WindowSize * sizeof(Element)) > 0;
			return  b2 ;
		}

		friend bool operator< (const ArrayElement_ &first, const ArrayElement_ &second)		// Ridefinizione dell'operatore <
		{
			if (first.WindowSize != second.WindowSize)
				return first.WindowSize < second.WindowSize;

			bool b2 = memcmp(first.pElement,second.pElement,first.WindowSize * sizeof(Element)) < 0;
			return  b2;
		}

	} ArrayElement;
	typedef std::map<ArrayElement,std::vector<unsigned int> > HashTable;	// Definizione del tipo HashTable come una tabella di Hash con chiave ArrayElement e valore un array di interi. Serve nella fase di ricerca delle stringhe all'interno del documento da comprimere: il 'valore' conterrà gli indirizzi iniziali di tutte le occorrenze della 'chiave' all'interno del documento.
	typedef std::map<ArrayElement, std::string> PreHeader;		// Definizione del tipo PreHeader come una tabella di Hash con chiave ArrayElement e valore una stringa. Serve nella fase di creazione dell'header: il 'valore' contiene la codifica compressa della 'chiave'.




	static bool Gainvalue_comparer(typename HashTable::value_type &i1, typename HashTable::value_type &i2)  // Funzione che, date due differenti stringhe nella codifica base (ovvero valori 'chiave' di una HashTable, confronta il guadagno che comporterebbe codificare la prima col guadagno che comporterebbe codificare la seconda.
	{

		int Gain1 = MinCodeSize * i1.second.size() * i1.first.WindowSize - X_V*8 - MinCodeSize * ( i1.second.size()+ i1.first.WindowSize+1); 	// Calcolo della funzione di guadagno per la prima stringa
		int Gain2 = MinCodeSize * i2.second.size() * i2.first.WindowSize - X_V*8 - MinCodeSize*( i2.second.size()+ i2.first.WindowSize+1);	// Calcolo della funzione di guadagno per la seconda stringa

		if (Gain1 == Gain2)		// Se i guadagni delle due stringhe sono uguali, considera come stringa più conveniente quella di lunghezza maggiore
			return i1.first.WindowSize < i2.first.WindowSize;	// Restituisce vero se la prima stringa è più corta della seconda, falso altrimenti

		return Gain1 < Gain2;	// Restituisce vero se la prima stringa garantisce un guadagno maggiore della seconda, falso altrimenti
	}

	static bool Occvalue_comparer(typename HashTable::value_type &i1, typename HashTable::value_type &i2)	// Funzione che, date due differenti stringhe nella codifica base, confronta il loro numero di occorrenze.
	{
		return i1.second.size() >  i2.second.size(); // Restituisce vero se la prima stringa ha più occorrenze della seconda, falso altrimenti
	}


#pragma endregion

	TextCompressor(int X) : X(X) {}
	int Compress(const void *data, const unsigned int size, const unsigned int strength,void **cdata)	// Funzione che opera la compressione di un file in ingresso, di dimensione size byte, con parametro di qualità strength
	{

#pragma region Step 1 - (Source symbols retrieval and encoding len computation)
		X_V = X;
		size_t ptrSize = 1 << (chunkByteSize * 8) ;	// Definisce 'ptrsize' come 2^(chunkByteSize*8), ovvero il massimo numero di possibili parole-codice differenti nella codifica originale

		Element *ptr = new Element[ptrSize];	// Crea una zona di memoria in cui inserire fino a 'ptrsize' oggetti Element. Il primo indirizzo di quest'area di memoria è puntato da 'ptr'. Quest'area è di fatto un array di Element
		memset(ptr,0,ptrSize * sizeof(Element));	// Setta tutta l'area di memoria puntata da 'ptr' a '0'
		memset(ptr,-1,sizeof(Element));		// Setta l'area di memoria puntata da 'ptr', destinati a contenere il primo Element, a '-1'


		for (unsigned int i = 0; i < size / chunkByteSize; i++)		// Scorre tutto il documento, simbolo per simbolo (la posizione di ciascun simbolo all'interno del documento è dato proprio dal rapporto tra la dimensione in byte del documento, 'size' e la dimensione in byte dei simboli 'chunkByteSize')
		{
			UINT64 val = 0;
			memcpy(&val,((Element*)data) + i,chunkByteSize);
			memcpy(ptr+val,&val,sizeof(Element));			// Inserisce il simbolo nell'area di memoria puntata da 'ptr', nella posizione indicata da 'val' stesso. (e.g.: L'elemento con codifica binaria 1245 si trova alla 1245° posizione all'interno dell'area di memoria)
		}

		unsigned int NumChars = 0;	// NumChars indica quanti simboli differenti sono utilizzati nel documento originale, ovvero di quanti simboli è composto l'alfabeto di base. Viene inizializzata a 0


		for (unsigned int i = 0; i < ptrSize; i++)	// Si scorrono tutti i 'ptrSize' Element memorizzati nell'area di memoria puntata da 'ptr'. 
		{
			char buf[sizeof(Element)];	// Per scorrere l'area di memoria utilizza un buffer 'buf' della stessa dimensione in bit di un Element
			memset(buf,i==0 ? -1 : 0,sizeof(Element)); // Inizializza il buffer al valore -1 alla prima iterazione, 0 altrimenti. Il valore -1 non può comparire in nessuna cella dell'area di memoria, poichè le rappresentazioni decimali dei simboli sono definite positive. 

			if (memcmp(ptr + i, buf,sizeof(Element))!=0)	// Se il contenuto del buffer è differente dal contenuto della i-esima cella, significa che la i-esima cella contiene un Element. In tal caso esegue del codice:
			{
				ArrayElement elem(1,ptr + i);		// Istanzia un ArrayElement 'elem' di WindowSize 1, contenente il simbolo memorizzato nella cella di memoria
				auto el = typename PreHeader::value_type(elem,"");	// Istanzia un elemento 'el' di PreHeader, contenente l'ArrayElement come 'chiave' ed una stringa vuota come 'valore' (vuota poichè ancora non è stata scelta una codifica per quel simbolo)
				this->ph.insert(el);	// Inserisce l'elemento 'el' nel PreHeader 'ph'

				++NumChars;		// Incrementa NumChars
			}
		}

		if (NumChars == 1)
			MinCodeSize = 1;
		else
			MinCodeSize = static_cast<unsigned int>(ceil( log(static_cast<float>(NumChars))/log(2.f))); 	// Inserisce in MinCodeSize il più piccolo multiplo di 2 che contenga NumChars

#pragma endregion
#pragma region Step 2 - (Strings search)
		unsigned int FreeSymbols = (1 << MinCodeSize) - NumChars;	// Calcola il numero di simboli liberi e li inserisce in FreeSymbols
		unsigned int MaxElements = strength * FreeSymbols;	// MaxElements è usato per ridurre i tempi necessari alla ricerca delle stringhe nel documento, ed è proporzionale al numero di simboli liberi ed alla qualità della compressione che si desidera ottenere



		std::vector<int> UniqueWindowSizes;	// 'UniqueWindowSizes' è un vettore che contiene tutte e sole le differenti dimensioni delle stringhe che andremo a codificare. Servirà in fase di compressione del body
		UniqueWindowSizes.push_back(1);		// Sicuramente andremo a codificare tutti i caratteri, quindi '1' va sicuramente inserito in 'UniqueWindowSizes'

		if (FreeSymbols != 0 && MaxElements != 0)
		{
			std::vector<HashTable> Tables;	// Istanzia un vettore di HashTable 'Tables'
			unsigned int CurrentLenght = 2;	
			HashTable ht;			// Istanzia una HashTable 'ht'



			bool goOn = true;	
			do				// Esegue il ciclo fino a che non vengono trovate tutte stringhe ad occorrenza singola. 'CurrentLenght' indica la dimensione in Element delle stringhe che si considerano, viene incrementata ad ogni iterazione 
			{
				ArrayElement el;	// Crea un ArrayElement 'el' di WindowSize 'CurrentLenght' che verrà usato per eseguire i confronti.
				el.WindowSize = CurrentLenght;		
				el.pElement = new Element[CurrentLenght];
				memset(el.pElement,0,sizeof(Element) * CurrentLenght);

				if (CurrentLenght == 2)		// Se stiamo considerando stringhe di lunghezza 2:
				{
					for (unsigned int i = 1; i < ((size/sizeof(Element)) - (CurrentLenght - 1)); i++)	// Scorre tutto il documento, prendendo 2 Element per volta (L'ultimo indice, dunque, non corrisponde all'ultimo Element, ma al penultimo. Più in generale, all'ultimo - (CurrentLenght-1)
					{
						memcpy(el.pElement,(Element*)data + i,CurrentLenght * sizeof(Element));		// Inserisce la stringa da 2 Element in 'el' 

						HashTable::iterator val = ht.find(el);		// Cerca la stringa contenuta nell'ArrayElement 'el' nella HashTable 'ht'

						if ( val != ht.end())			// Se non lo trova, lo inserisce
						{
							val->second.push_back(i);
						}
						else					// Altrimenti inserisce l'indirizzo in cui si trova l'attuale occorrenza della stringa contenuta in 'el', nel vettore delle occorrenze dell'ArrayElement relativo
						{
							std::vector<unsigned int> t;
							t.push_back(i);
							ht.insert(std::pair<ArrayElement,std::vector<unsigned int> > (el,t));
						}
					}
				}
				else		// Se stiamo considerando stringhe di lunghezza maggiore di 2
				{
					int i = 0;
					for (HashTable::const_iterator it = Tables[CurrentLenght - 3].begin(); it != Tables[CurrentLenght - 3].end(); it++)	// Scorre la HashTable contenente le stringhe di lunghezza immediatamente inferiore alla corrente. 'CurrentLenght-3' è dovuto al fatto che la prima HashTable (Table[0]) è quella di WindowSize 2 
					{

						for (std::vector<unsigned int>::const_iterator pos = it->second.begin(); pos != it->second.end(); pos++)	// Per ogni ArrayElement contenuto nella HashTable, scorre l'intero vettore delle occorrenze
						{
							memcpy(el.pElement,(Element*)data + *pos,CurrentLenght * sizeof(Element));	// Inserisce la stringa da 'CurrentLenght' Element in 'el'

							HashTable::iterator val = ht.find(el);		// Cerca la stringa contenuta nell'ArrayElement 'el' nella HashTable 'ht'

							if ( val != ht.end())		// Se non lo trova, lo inserisce
							{
								val->second.push_back(*pos);
							}
							else		// Altrimenti inserisce l'indirizzo in cui si trova l'attuale occorrenza della stringa contenuta in 'el', nel vettore delle occorrenze dell'ArrayElement relativo
							{
								std::vector<unsigned int> t;
								t.push_back(*pos);
								ht.insert(std::pair<ArrayElement,std::vector<unsigned int>> (el,t));
							}
						}
					}
				}

				for (typename HashTable::iterator el = ht.begin(); el != ht.end();)		// Scorre tutta la HashTable 'ht' eliminando gli elementi che compaiono con una sola occorrenza
				{
					if (el->second.size() <= 1)		// Quando elimina un elemento dalla tabella, la struttura dati automaticamente shifta tutti gli elementi seguenti in alto di 1 posizione, quindi non c'è bisogno di incrementare l'iteratore
						ht.erase(el++);
					else
						++el;
				}

				if (ht.size() == 0 || CurrentLenght == 256)		// Se la tabella attuale non contiene elementi, allora sicuramente nessuna delle seguenti potrà contenerne e dunque si può uscire dal ciclo
					goOn = false;		// goOn è la condizione di iterazione, che viene impostata a false: la prossima iterazione non sarà eseguita ed il ciclo finirà
				else				// Se nella tabella attuale sono rimasti degli elementi, allora il ciclo dovrà continuare. 
				{
					if (MaxElements == 0)
					{
						ht.clear();
						goOn = false;
						break;
					}

					MaxElements--;

					if (ht.size() <= MaxElements)	// Se nella tabella sono presenti al più 'MaxElements' elementi:
					{
						Tables.push_back(ht);	// Si inserisce in Table la HashTable 'ht'
						++CurrentLenght;	// Si incrementa CurrentLenght
						ht.clear();		// Si svuota completamente 'ht' così da renderlo disponibile all'iterazione successiva
						continue;
					}


					unsigned int cycles = ht.size() - MaxElements;	// Se nella tabella sono presenti più di 'MaxElements' elementi, allora elimina tutte le eccedenze, prendendo ogni volta l'ArrayElement con meno occorrenze

					for (unsigned int i = 0; i < cycles; i++)
					{
						typename HashTable::iterator themin = std::max_element(ht.begin(),ht.end(),Occvalue_comparer);
						if (themin != ht.end())
							ht.erase(themin);
					}

					Tables.push_back(ht);	// Si inserisce in Table la HashTable 'ht'
					++CurrentLenght;	// Si incrementa CurrentLenght
					ht.clear();		// Si svuota completamente 'ht' così da renderlo disponibile all'iterazione successiva
				}



			} while(goOn);

			EraseTablesForUnusefulStrings(Tables);  // Elimina dalle tabelle tutte le stringhe che non conviene codificare. All'interno valuta per ogni stringa la funzione di guadagno

			HashTable htt;			// La HashTable 'htt' viene creata per contenere TUTTE le stringhe differenti contenute nel vettore di HashTables 'Tables', senza dividerle per lunghezza ('WindowSize')
			for (unsigned int k = 0; k < Tables.size(); k++)
			{
				if (Tables[k].size() != 0)
					htt.insert(Tables[k].begin(),Tables[k].end());
			}
#pragma endregion

#pragma region Step 3 - (Strings selection)

			for (unsigned short i = 0; i < FreeSymbols && htt.size() > 0; i++)		// Per tante volte quanti sono i simboli liberi
			{
				typename HashTable::iterator themax = std::max_element(htt.begin(),htt.end(),Gainvalue_comparer);	// Prende da 'htt' la stringa 'themax' che in assoluto ha la funzione di guadagno maggiore
				this->ph.insert(typename PreHeader::value_type(themax->first,""));					// Inserisce 'themax' in 'ph': significa che 'themax' è stata scelta per essere codificata con un simbolo dell'alfabeto esteso

				if (std::find(UniqueWindowSizes.begin(),UniqueWindowSizes.end(),themax->first.WindowSize) == UniqueWindowSizes.end())
					UniqueWindowSizes.push_back(themax->first.WindowSize);						// Se la dimensione in caratteri della stringa 'themax' non è ancora presente in 'UniqueWindowSize' allora la inserisce


				for(auto it = htt.begin(); it != htt.end(); it++)			// Scorri tutto 'htt' prendendone di volta in volta tutti gli elementi. 'it' punta all'elemento corrente
				{
					/*
					if (ElemInElem(themax->first,it->first) == false)		// Se l'elemento puntato da 'it' non può sicuramente sovrapporsi con 'themax', allora passa al prossimo elemento 
					continue;
					*/
					unsigned int max_sec_size = themax->second.size();		// 'max_sec_size' contiene il numero di occorrenze della stringa 'themax'

					for (unsigned int k = 0; k < max_sec_size; k++)			// Scorri tutte le occorrenze di 'themax'
					{

						unsigned int it_second_size = it->second.size();	// 'it_second_size' contiene il numero di occorrenze della stringa puntata da 'it' 
						for (unsigned int j = 0; j < it_second_size; j++)	// Scorri tutte le occorrenze di 'it'
						{
							if (themax->second[k] + themax->first.WindowSize - 1 < it->second[j])	// Sfrutta il fatto che i vettori delle occorrenze delle due stringhe sono ordinati. Se l'occorrenza attuale di 'themax' termina prima dell'inizio dell'occorrenza attuale di 'it', allora essa terminerà sicuramente prima dell'inizio di tutte le occorrenze successive di 'it', e quindi si può passare alla successiva occorrenza di 'themax' senza completare il ciclo  
								break;
							if(			
								it->first != themax->first &&		// Se non stiamo confrontando due occorrenze della stessa stringa e
								(
								((it->second[j] >= themax->second[k] &&
								it->second[j] <= themax->second[k] + themax->first.WindowSize - 1) ||	

								(it->second[j] + it->first.WindowSize - 1 >= themax->second[k] &&
								it->second[j] + it->first.WindowSize - 1 <= themax->second[k] + themax->first.WindowSize - 1))	// Se le due stringhe non si sovrappongono (un estremo dell'una è contenuto nell'intervallo tra i due estremi dell'altra)
								)
								)

							{
								it->second.erase(it->second.begin() + j);	// Elimina l'occorrenza attuale di 'it'
								j--;						// La struttura shifta all'indietro i restanti elementi nel vettore delle occorrenze di 'it', per cui alla successiva iterazione dovremo ripartire dallo stesso indice 'j' (dunque lo decrementiamo, poichè sarà automaticamente incrementato alla fine dell'iterazione)
								it_second_size--;				// Aggiorniamo il numero di occorrenze di 'it'
							}
						}
					}
				}

				htt.erase(themax->first);		// Alla fine di tutto questo, possiamo eliminare da 'htt' l'elemento 'themax', dato che non ci servirà più per futuri confronti

				auto it = htt.begin();			// In seguito ad ogni iterazione potrebbero essere state cancellate tutte le occorrenze di alcuni elementi in 'htt', dunque provvediamo ad eliminarle
				while(it != htt.end())			
				{
					if (it->second.size() == 0)
					{
						htt.erase(it->first);
						it = htt.begin();
					}
					else
						it++;
				}

				std::vector<HashTable> vc;		
				vc.insert(vc.begin(),htt);
				EraseTablesForUnusefulStrings(vc);
				htt = *(vc.begin());

			}
		}
#pragma endregion

#pragma region Step 4 (Header building)
		std::sort(UniqueWindowSizes.begin(),UniqueWindowSizes.end(),std::greater<int>());	// Ordina il vettore 'UniqueWindowSize' in maniera decrescente, poichè è questo l'ordine con cui dovremo scorrerlo in fase di compressione del documento.

		UINT Sum = 0;
		for (auto it = ph.begin(); it != ph.end(); it++)
		{
			if (it->first.WindowSize > 1)
				Sum += (8 + MinCodeSize + (MinCodeSize * it->first.WindowSize));			// 'Sum' contiene la dimensione, in bit, della Tabella di Compressione Stringhe (che è una sezione dell'header) 
		}

		size_t HSize = ((24 + Sum +  (	NumChars * (MinCodeSize + (8 * chunkByteSize) ) )) + MinCodeSize);	// 'HSize' contiene la dimensione in bit dell'header
		std::string header;
		std::string st;						// 'st' è una stringa temporanea in cui inseriremo di volta in volta una stringa di bit che dev'essere scritta nell'header
		boost::dynamic_bitset<unsigned __int64> bt;		// 'bt' è un'area di memoria temporanea, utilizzata per contenere di volta in volta la rappresentazione binaria a 64 bit dell'intero decimale che di volta in volta dobbiamo codificare per inserirlo nell'header

		bt.append(8*chunkByteSize);				// 'bt' contiene il numero di bit della codifica originale
		boost::to_string(bt,st);				// Scriviamo in 'st' il contenuto di 'bt', trasformato in stringa
		st = st.substr(st.length()-8,std::string::npos);	// Prendiamo gli ultimi 8 caratteri (quelli corrispondenti agli 8 bit meno significativi) di 'st'
		header.append(st);					// Inseriamo 'st' nell'header
		bt.clear();						// Puliamo il buffer 'bt'
		bt.append(MinCodeSize);					// 'bt' contiene il numero di bit della codifica compressa
		boost::to_string(bt,st);				// Scriviamo in 'st' il contenuto di 'bt', trasformato in stringa
		st = st.substr(st.length()-8,std::string::npos);	// Prendiamo gli ultimi 8 caratteri (quelli corrispondenti agli 8 bit meno significativi) di 'st'
		header.append(st);					// Inseriamo 'st' nell'header

		int q = 0;

		for (typename PreHeader::iterator it = ph.begin(); it != ph.end(); it++,q++)	// Scorriamo 'ph' alla ricerca di tutti i simboli dell'alfabeto di base (ovvero tutti i caratteri di cui è composto il documento in input), dunque tutti gli ArrayElement con 'Windowsize' pari ad '1'
		{
			if (it->first.WindowSize != 1)
				continue;

			bt.clear();			
			bt.append(q);								// Inseriamo in 'bt' la posizione del simbolo corrente di 'ph' (Utilizzeremo come sua codifica compressa, proprio questo indice)
			boost::to_string(bt,st);						// Scriviamo in 'st' il contenuto di 'bt', trasformato in stringa 
			st = st.substr(st.length() - MinCodeSize,std::string::npos);		// Prendiamo gli ultimi 'MinCodeSize' caratteri (quelli corrispondenti ai 'MinCodeSize' bit meno significativi) di 'st'
			header.append(st);							// Inseriamo 'st' nell'header
			it->second = st;							// Scriviamo 'st' come 'valore' di 'ph' relativo alla 'chiave' rappresentata dal carattere corrente.
			unsigned __int64 val = 0;						// 'val' è un'area di memoria temporanea omogenea ad un intero a 64 bit
			memcpy(&val,it->first.pElement,chunkByteSize);				// Scriviamo in 'val' l'intero che rappresenta (nella codifica originale) il simbolo corrente
			bt.clear();							
			bt.append(val);								// Scriviamo in 'bt' il contenuto di 'val'
			boost::to_string(bt,st);						// Scriviamo in 'st' il contenuto di 'bt', trasformato in stringa  
			st = st.substr(st.length() - 8*chunkByteSize,std::string::npos);	// Prendiamo gli ultimi '8*ChunkByteSize' caratteri (quelli corrispondenti agli '8*ChunkByteSize' bit meno significativi) di 'st' 
			header.append(st);							// Inseriamo 'st' nell'header

		}

		for (unsigned int i = 0; i < MinCodeSize; i++)					// Inseriamo 'MinCodeSize' simboli '0', per separare la Tabella di compressione dei caratteri dalla Tabella di compressione delle stringhe
			header.append("0");

		bt.clear();

		q = 0;
		for (typename PreHeader::iterator it = ph.begin(); it != ph.end(); it++,q++)	// Scorriamo 'ph' alla ricerca di tutti i simboli dell'alfabeto esteso (ovvero tutte le stringhe che abbiamo scelto di codificare), dunque tutti gli ArrayElement con 'Windowsize' diverso da '1'	
		{
			if (it->first.WindowSize == 1)
				continue;

			bt.clear();								
			bt.append(it->first.WindowSize);					// Inseriamo in 'bt' la lunghezza in caratteri della stringa corrente
			boost::to_string(bt,st);						// Scriviamo in 'st' il contenuto di 'bt', trasformato in stringa  
			st = st.substr(st.length() - 8,std::string::npos);			// Prendiamo gli ultimi 8 caratteri (quelli corrispondenti agli 8 bit meno significativi) di 'st' 
			header.append(st);							// Inseriamo 'st' nell'header


			bt.clear();
			bt.append(q);								// Inseriamo in 'bt' la posizione del simbolo corrente di 'ph' (Utilizzeremo come sua codifica compressa, proprio questo indice)
			boost::to_string(bt,st);						// Scriviamo in 'st' il contenuto di 'bt', trasformato in stringa
			st = st.substr(st.length() - MinCodeSize,std::string::npos);		// Prendiamo gli ultimi 'MinCodeSize' caratteri (quelli corrispondenti ai 'MinCodeSize' bit meno significativi) di 'st'
			header.append(st);							// Inseriamo 'st' nell'header
			it->second = st;							// Scriviamo 'st' come 'valore' di 'ph' relativo alla 'chiave' rappresentata dalla stringa corrente.

			for (unsigned int z = 0; z < it->first.WindowSize; z++)								// Scorre tutti i caratteri che compongono la stringa corrente 
			{
				for (typename PreHeader::iterator iz = ph.begin(); iz != ph.end(); iz++)				// Scorre 'ph' mediante un iteratore 'iz'
				{

					if (iz->first.WindowSize != 1)									// Se 'iz' non punta ad un carattere, passa oltre 
						continue;

					if (memcmp((void*)iz->first.pElement,(void*)(it->first.pElement+z),sizeof(Element)) == 0)	// Se 'iz' punta ad un carattere, confronta il carattere corrente quello puntato da 'iz', se i caratteri sono diversi, passa oltre
					{
						header.append(iz->second);						// Inseriamo la codifica in binario del carattere nell'header
						break;											// Passiamo al successivo carattere della stringa
					}
				}
			}
		}



		unsigned int ZeroAdds = 8 - (header.length() % 8);				// Calcola quanti '0' sono necessari per allineare l'header ad un multiplo di 8 bit e lo scrive in 'ZeroAdds'

		for (unsigned int i = 0; i < ZeroAdds; i++)					// Inserisce 'ZeroAdds' caratteri '0' in coda all'header
			header.append("0");							

		header.append("00000000");							// Inserisce 8 bit '0' in coda all'header. Questa sequenza segnala al decoder che l'header è finito


#pragma endregion
#pragma region Step 5 - (Compressed document writing)
		std::string body = "1";					// Inserisce un bit '1' in testa al body, per indicare al decoder che deve iniziare a decodificare il body dal bit successivo

		Element *pText = (Element*)data;		
		UINT charLen = size/chunkByteSize;		// Esprime la dimensione del documento da comprimere in carattere invece che in byte

		for(unsigned int offset = 0; offset < charLen;)		// Scorre tutto il documento. 'offset', che è l'indice del for (anche se non viene incrementato in maniera fissa) indica quale carattere del documento stiamo analizzando
		{
			for(typename std::vector<int>::iterator it = UniqueWindowSizes.begin(); it != UniqueWindowSizes.end(); it++)	// Usa una window la cui dimensione è assegnata scorrendo l'uno dopo l'altro il vettore 'UniqueWindowSize' (che ricordiamo è ordinato decrescente)
			{
				if (offset + *it > (charLen))		// Se la finestra scelta è più grande del resto del documento, passa alla successiva dimensione della finestra
					continue;

				ArrayElement el(*it,pText+offset);	// Inserisce in un ArrayElement temporaneo, 'el', il contenuto della finestra
				typename PreHeader::iterator wt;	
				wt = ph.find(el);			// Cerca all'interno di 'ph' l'elemento con 'chiave' 'el'

				if (wt != ph.end())			// Se lo trova:
				{
					body.append(wt->second);	// Inserisce il 'valore' corrispondente alla 'chiave' contenuta in 'el' nel body
					offset+= *it;			// Incrementa l'indice del ciclo della dimensione della window usata (la prossima iterazione riprenderà dal carattere successivo all'ultimo carattere della stringa appena trovata)
					break;
				}
				else if (*it == 1)
				{
					throw;
				}
			}
		}

		ZeroAdds = 8 - (body.length() % 8);			// Calcola quanti '0' sono necessari per allineare il 'body' ad un multiplo di 8 bit e lo scrive in 'ZeroAdds' 

		for (unsigned int i = 0; i < ZeroAdds; i++)		// Inserisce 'ZeroAdds' caratteri '0' in testa al body
			body = "0" + body;

		std::string file = header + body;			// Definisce il file compresso come concatenazione di header e body

		boost::dynamic_bitset<unsigned char> btset(file);	// Trasforma la stringa 'file' (contenente la rappresentazione binaria dei caratteri che costituiscono il file) in una vera e propria sequenza di caratteri ad 8 bit, 'btset'
		std::vector<unsigned char> Chars;			// Definisce un array di caratteri, 'Chars'
		Chars.resize(btset.num_blocks());			// Ridefinisce la dimensione di 'Chars' come lunghezza in caratteri di 'btset'
		boost::to_block_range(btset,Chars.begin());		// Inserisce carattere per carattere 'btset' in 'Chars'

		(*cdata) = new char[btset.num_blocks()];
		memcpy(*cdata,(void*)&Chars[0],btset.num_blocks());	// Sovrascrive l'area di memoria 'data' (in cui all'inizio abbiamo inserito il documento originale) col contenuto di 'Chars', ovvero col documento compresso. Ricordiamo che 'data' è passato come parametro, alla funzione, per riferimento: questo significa che al termine della funzione potremo disporre del suo contenuto.

		delete[] ptr;

		return btset.num_blocks();				// Il valore di ritorno della funzione è il numero di caratteri da cui è composto il documento compresso. Questa informazione, unita al contenuto di 'data', ci permette di scrivere il file compresso.
#pragma endregion
	}

	unsigned int  Decompress(const void *data, const unsigned int size, void **cdata)
	{
#pragma region Step 1 - (Header extraction)
		boost::dynamic_bitset<unsigned char> bt_set;
		boost::dynamic_bitset<unsigned long> bt_temp;
		std::string st = "";
		for (unsigned int i = 0; i < size; i++)
		{
			bt_set.append(((unsigned char*)data)[i]);
		}

		boost::to_string(bt_set,st);

		unsigned char chunkByteSize = ((unsigned char*)data)[size - 1] / 8;
		unsigned char MinCodeSize = ((unsigned char*)data)[size - 2];

		std::map<std::string,std::string> DecompressionTable;

		st = st.substr(16,std::string::npos);

		std::string currkey = "";
		std::string currvalue = "";
		int offset = 0;
		int zerocount = 0;

		currkey = st.substr(offset,MinCodeSize);
		offset+= MinCodeSize;
		currvalue = st.substr(offset, 8 * chunkByteSize);
		offset +=  8 * chunkByteSize;

		DecompressionTable.insert(std::map<std::string,std::string>::value_type(currkey,currvalue));

		while (true)
		{
			currkey = st.substr(offset,MinCodeSize);
			offset+= MinCodeSize;
			if (currkey == std::string(MinCodeSize,'0'))
			{
				break;
			}

			currvalue = st.substr(offset, 8 * chunkByteSize);	
			offset +=  8 * chunkByteSize;
			DecompressionTable.insert(std::map<std::string,std::string>::value_type(currkey,currvalue));
		}

		while (true)
		{
			currvalue = st.substr(offset,8);
			offset+= 8;
			if (currvalue == std::string(8,'0'))
			{

				break;
			}

			bt_temp = boost::dynamic_bitset<unsigned long>(currvalue);
			ULONG len = bt_temp.to_ulong();
			currkey = st.substr(offset,MinCodeSize);
			offset+=MinCodeSize;
			currvalue.clear();

			for (unsigned int q = 0; q < len; q++)
			{
				std::string tmpValue = st.substr(offset,MinCodeSize);
				std::map<std::string,std::string>::iterator it;
				it = DecompressionTable.find(tmpValue);
				if (it != DecompressionTable.end())
				{
					currvalue.append(it->second);
					offset+= MinCodeSize;			
				}
				else
					throw;
			}

			DecompressionTable.insert(std::map<std::string,std::string>::value_type(currkey,currvalue));

		}

		while (st.substr(offset,1) !=  "1")
		{	
			offset++;
		}

		offset++;

		std::string decompressed;

		auto iterations = (st.length() - offset) / MinCodeSize;
		for (unsigned int w = 0; w < iterations; w++)
		{
			currvalue = st.substr(offset,MinCodeSize);
			typename std::map<std::string,std::string>::iterator v_t;
			v_t = DecompressionTable.find(currvalue);
			if (v_t != DecompressionTable.end())
			{
				decompressed.append(v_t->second);
			}
			else
			{
				throw;
			}

			offset+= MinCodeSize;
		}
#pragma endregion
#pragma region Step 2 - (Document decoding)
		boost::dynamic_bitset<unsigned char> btset(decompressed);	// Trasforma la stringa 'file' (contenente la rappresentazione binaria dei caratteri che costituiscono il file) in una vera e propria sequenza di caratteri ad 8 bit, 'btset'
		std::vector<unsigned char> Chars;			// Definisce un array di caratteri, 'Chars'
		Chars.resize(btset.num_blocks());			// Ridefinisce la dimensione di 'Chars' come lunghezza in caratteri di 'btset'
		boost::to_block_range(btset,Chars.begin());		// Inserisce carattere per carattere 'btset' in 'Chars'

		std::vector<unsigned char> RevChars;
		for (auto it = Chars.rbegin(); it != Chars.rend(); it+=chunkByteSize)
		{
			for (int e = 0; e < chunkByteSize; e++)
			{
				RevChars.push_back(*(it+(chunkByteSize-1-e)));
			}
		}


		(*cdata) = new char[btset.num_blocks()];
		memcpy(*cdata,(void*)&RevChars[0],btset.num_blocks());	// Sovrascrive l'area di memoria 'data' (in cui all'inizio abbiamo inserito il documento originale) col contenuto di 'Chars', ovvero col documento compresso. Ricordiamo che 'data' è passato come parametro, alla funzione, per riferimento: questo significa che al termine della funzione potremo disporre del suo contenuto.

		return btset.num_blocks();				// Il valore di ritorno della funzione è il numero di caratteri da cui è composto il documento compresso. Questa informazione, unita al contenuto di 'data', ci permette di scrivere il file compresso.
#pragma endregion
	}

private:
	const unsigned int X;
	PreHeader ph;

	void EraseTablesForUnusefulStrings(std::vector<HashTable> &Tables)		// Funzione che, dato un vettore di HashTable, 'Table':
	{
		for ( unsigned int i = 0; i < Tables.size(); i++)			// Scorre gli elementi di 'Table', ovvero tutte le HashTable di WindowSize differenti
		{
			float O_cl =  ((static_cast<float>(i)+2.f+1.f)/(static_cast<float>(i)+2.f-1.f)) + (static_cast<float>(X)*8.f)/(static_cast<float>(MinCodeSize)*(static_cast<float>(i)+2.f-1.f));	// Calcola, per ciascuna HashTable qual è il numero minimo di occorrenze necessarie perchè un suo elemento risulti vantaggioso da codificare e lo inserisce in 'o_cl'
			for(HashTable::iterator val = Tables[i].begin(); val != Tables[i].end();)	// Scorre tutti gli ArrayElements della HashTable corrente
			{

				if (val->second.size() <= O_cl)		// Se il numero di occorrenze dell'elemento corrente non è maggiore di 'o_cl', cancella l'elemento
				{
					Tables[i].erase(val++);
				}
				else
					++val;
			}
		}
	}

	inline bool ElemInElem(const ArrayElement &a,const  ArrayElement &b)		// Questa funzione verifica che le stringhe contenute in 2 ArrayElement qualsiasi possano sovrapporsi
	{

		/* eMin, eMax, p1 e p2 non vengono mai utilizzati O.ò

		const ArrayElement &eMin = (a.WindowSize > b.WindowSize ? b : a);	// Inseriamo in 'eMin' l'ArrayElement che contiene la stringa di lunghezza minore
		const ArrayElement &eMax = (a.WindowSize > b.WindowSize ? a : b);	// Inseriamo in 'eMax' l'ArrayElement che contiene la stringa di lunghezza maggiore



		std::wstring p1 = ((ArrayElement)a).ToString();				// Estraiamo la stringa contenuta nell'ArrayElement 'a' e la inseriamo in 'p1' 
		std::wstring p2 = ((ArrayElement)b).ToString();				// Estraiamo la stringa contenuta nell'ArrayElement 'b' e la inseriamo in 'p2' 

		*/

		Element *pFirst;
		Element *pSecond;


		for (unsigned int i = 0; i < a.WindowSize + b.WindowSize; i++)		
		{

			size_t size;

			/* Nell'if che segue, il seguente codice si ripete in tutti i suoi rami:

			pFirst = new Element[size];
			pSecond = new Element[size];

			Forse si può portare fuori dall'if */

			if (i < a.WindowSize)			
			{
				size = i + 1;
				pFirst = new Element[size];
				pSecond = new Element[size];

				memcpy(pFirst,a.pElement+(a.WindowSize-i-1),sizeof(Element) * (size));
				memcpy(pSecond,b.pElement,sizeof(Element) * size);

			}
			else if (i >= a.WindowSize && i <= b.WindowSize)
			{
				size = a.WindowSize;
				pFirst = new Element[size];
				pSecond = new Element[size];

				memcpy(pFirst,a.pElement,size * sizeof(Element));
				memcpy(pSecond,b.pElement+(i-a.WindowSize),size);
			}
			else
			{
				size = a.WindowSize + b.WindowSize - i;
				pFirst = new Element[size];
				pSecond = new Element[size];

				memcpy(pFirst,a.pElement,sizeof(Element) * (size));
				memcpy(pSecond,b.pElement+(i - a.WindowSize),sizeof(Element) * (size));
			}

			int res = memcmp(pFirst,pSecond,sizeof(Element)*size);

			delete[] pFirst;
			delete[] pSecond;

			if (res == 0)
				return true;
		}

		return false;
	}

};

ITextCompressor* CreateTextCompressor(int CBS) 
{
	if (CBS == 1)
		return new TextCompressor<1>(1);
	else if(CBS==2) 
		return new TextCompressor<2>(1); 
	else if (CBS==3) return new TextCompressor<3>(1);

	return NULL;
}